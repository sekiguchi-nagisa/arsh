/*
 * Copyright (C) 2021 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdlib>
#include <unistd.h>

#include "error_report.h"

namespace ydsh {

// ###########################
// ##     ErrorReporter     ##
// ###########################

/**
 * not allow dumb terminal
 */
static bool isSupportedTerminal(int fd) {
  const char *term = getenv(ENV_TERM);
  return term != nullptr && strcasecmp(term, "dumb") != 0 && isatty(fd) != 0;
}

static std::vector<std::string> split(const std::string &str) {
  std::vector<std::string> bufs;
  bufs.emplace_back();
  for (auto ch : str) {
    if (ch == '\n') {
      bufs.emplace_back();
    } else {
      bufs.back() += ch;
    }
  }
  if (!str.empty() && str.back() == '\n') {
    bufs.pop_back();
  }
  return bufs;
}

static const char *toString(DSErrorKind kind) {
  switch (kind) {
  case DS_ERROR_KIND_PARSE_ERROR:
    return "syntax error";
  case DS_ERROR_KIND_TYPE_ERROR:
    return "semantic error";
  case DS_ERROR_KIND_CODEGEN_ERROR:
    return "codegen error";
  default:
    return "";
  }
}

ErrorReporter::ErrorReporter(DSError *e, FILE *fp, bool close)
    : dsError(e), fp(fp), close(close), tty(isSupportedTerminal(fileno(fp))) {}

ErrorReporter::~ErrorReporter() {
  if (this->close) {
    fclose(this->fp);
  }
}

void ErrorReporter::printError(const Lexer &lex, const char *kind, Token token, TermColor c,
                               const char *message) {
  unsigned lineNumOffset = lex.getLineNumOffset();
  fprintf(this->fp, "%s:", lex.getSourceName().c_str());
  if (lineNumOffset > 0) {
    auto srcPos = lex.getSrcPos(token);
    fprintf(this->fp, "%d:%d:", srcPos.lineNum, srcPos.chars);
  }
  fprintf(this->fp, " %s%s[%s]%s %s\n", this->color(c), this->color(TermColor::Bold), kind,
          this->color(TermColor::Reset), message);

  if (lineNumOffset > 0) {
    this->printErrorLine(lex, token);
  }
  fflush(this->fp);
}

const char *ErrorReporter::color(TermColor c) const {
  if (this->tty) {
#define GEN_STR(E, C) "\033[" #C "m",
    const char *ansi[] = {EACH_TERM_COLOR(GEN_STR)};
#undef GEN_STR
    return ansi[static_cast<unsigned int>(c)];
  }
  return "";
}

void ErrorReporter::printErrorLine(const Lexer &lexer, Token errorToken) const {
  Token lineToken = lexer.getLineToken(errorToken);
  auto line = lexer.formatTokenText(lineToken);
  auto marker = lexer.formatLineMarker(lineToken, errorToken);

  auto lines = split(line);
  auto markers = split(marker);
  size_t size = lines.size();
  assert(size == markers.size());
  bool omitLine = size > 30;
  std::pair<size_t, size_t> pairs[2] = {{0, omitLine ? 15 : size},
                                        {omitLine ? size - 10 : size, size}};
  for (unsigned int i = 0; i < 2; i++) {
    if (i == 1 && omitLine) {
      fprintf(this->fp, "%s%s%s\n", this->color(TermColor::Yellow),
              "\n| ~~~ omit error lines ~~~ |\n", this->color(TermColor::Reset));
    }

    size_t start = pairs[i].first;
    size_t stop = pairs[i].second;
    for (size_t index = start; index < stop; index++) {
      // print error line
      fprintf(this->fp, "%s%s%s\n", this->color(TermColor::Cyan), lines[index].c_str(),
              this->color(TermColor::Reset));

      // print line marker
      fprintf(this->fp, "%s%s%s%s\n", this->color(TermColor::Green), this->color(TermColor::Bold),
              markers[index].c_str(), this->color(TermColor::Reset));
    }
  }
  fflush(this->fp);
}

bool ErrorReporter::handleParseError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                     const ParseError &parseError) {
  return this->handleError(ctx, DS_ERROR_KIND_PARSE_ERROR, parseError.getErrorKind(),
                           parseError.getErrorToken(), parseError.getMessage().c_str());
}

bool ErrorReporter::handleTypeError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                    const TypeCheckError &checkError) {
  return this->handleError(ctx, DS_ERROR_KIND_TYPE_ERROR, checkError.getKind(),
                           checkError.getToken(), checkError.getMessage());
}

bool ErrorReporter::handleCodeGenError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                       const CodeGenError &codeGenError) {
  return this->handleError(ctx, DS_ERROR_KIND_CODEGEN_ERROR, codeGenError.getKind(),
                           codeGenError.getToken(), codeGenError.getMessage());
}

bool ErrorReporter::handleError(const std::vector<std::unique_ptr<FrontEnd::Context>> &ctx,
                                DSErrorKind type, const char *errorKind, Token errorToken,
                                const char *message) {
  auto &lexer = ctx.back()->lexer;
  errorToken = lexer.shiftEOS(errorToken);

  /**
   * show error message
   */
  this->printError(lexer, toString(type), errorToken, TermColor::Magenta, message);

  auto end = ctx.crend();
  for (auto iter = ctx.crbegin() + 1; iter != end; ++iter) {
    auto &node = (*iter)->srcListNode;
    Token token = node->getPathNode().getToken();
    auto &lex = (*iter)->lexer;
    this->printError(lex, "note", token, TermColor::Blue, "at module import");
  }

  auto srcPos = lexer.getSrcPos(errorToken);
  const char *sourceName = lexer.getSourceName().c_str();
  if (this->dsError) {
    *this->dsError = {.kind = type,
                      .fileName = strdup(sourceName),
                      .lineNum = srcPos.lineNum,
                      .chars = srcPos.chars,
                      .name = strdup(errorKind)};
  }
  return true;
}

} // namespace ydsh