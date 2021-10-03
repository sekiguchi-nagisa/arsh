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

#include "compiler.h"

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

// ######################
// ##     Compiler     ##
// ######################

Compiler::Compiler(DefaultModuleProvider &moduleProvider, std::unique_ptr<FrontEnd::Context> &&ctx,
                   CompileOption compileOption, const CompileDumpTarget *dumpTarget)
    : compileOption(compileOption), provider(moduleProvider),
      frontEnd(this->provider, std::move(ctx), toOption(this->compileOption)),
      uastDumper(dumpTarget ? dumpTarget->fps[DS_DUMP_KIND_UAST] : nullptr),
      astDumper(dumpTarget ? dumpTarget->fps[DS_DUMP_KIND_AST] : nullptr),
      codegen(this->provider.getPool()),
      codeDumper(dumpTarget ? dumpTarget->fps[DS_DUMP_KIND_CODE] : nullptr,
                 this->provider.getPool()) {
  if (this->uastDumper) {
    this->frontEnd.setUASTDumper(this->uastDumper);
  }
  if (this->astDumper) {
    this->frontEnd.setASTDumper(this->astDumper);
  }
}

int Compiler::operator()(ObjPtr<FuncObject> &func) {
  this->frontEnd.setupASTDump();
  if (!this->frontEndOnly()) {
    this->codegen.initialize(this->frontEnd.getCurModId(), this->frontEnd.getCurrentLexer());
  }
  while (this->frontEnd) {
    auto ret = this->frontEnd();
    if (!ret) {
      return 1;
    }

    if (this->frontEndOnly()) {
      continue;
    }

    switch (ret.kind) {
    case FrontEndResult::ENTER_MODULE:
      this->codegen.enterModule(this->frontEnd.getCurModId(), this->frontEnd.getCurrentLexer());
      break;
    case FrontEndResult::EXIT_MODULE:
      if (!this->codegen.exitModule(cast<SourceNode>(*ret.node))) {
        goto END;
      }
      break;
    case FrontEndResult::IN_MODULE:
      if (!this->codegen.generate(*ret.node)) {
        goto END;
      }
      break;
    default:
      break;
    }
  }
  this->frontEnd.teardownASTDump();
  assert(this->frontEnd.getContext().size() == 1);
  {
    auto &modType = this->provider.newModTypeFromCurContext(this->frontEnd.getContext());
    if (!this->frontEndOnly()) {
      func = this->codegen.finalize(this->frontEnd.getMaxLocalVarIndex(), modType);
    }
  }

END:
  if (this->codegen.hasError()) {
    auto &e = this->codegen.getError();
    this->errorReporter &&this->errorReporter->handleCodeGenError(this->frontEnd.getContext(), e);
    return 1;
  }
  if (hasFlag(this->compileOption, CompileOption::LOAD_TO_ROOT)) {
    auto ret = this->provider.getPool().getModTypeById(this->frontEnd.getCurModId());
    assert(ret);
    auto msg = this->provider.getScope()->importForeignHandles(
        this->provider.getPool(), cast<ModType>(*ret.asOk()), ImportedModKind::GLOBAL);
    if (!msg.empty()) {
      auto node = std::make_unique<EmptyNode>(Token{0, 0});
      auto error = createTCError<ConflictSymbol>(
          *node, msg.c_str(), this->frontEnd.getCurrentLexer().getSourceName().c_str());
      this->errorReporter &&this->errorReporter->handleTypeError(this->frontEnd.getContext(),
                                                                 error);
      return 1;
    }
  }

  // dump code
  if (this->codeDumper && func) {
    this->codeDumper(func->getCode(), this->provider.getScope()->getMaxGlobalVarIndex());
  }
  return 0;
}

} // namespace ydsh