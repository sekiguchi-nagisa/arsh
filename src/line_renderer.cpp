/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "line_renderer.h"
#include "misc/word.hpp"

namespace ydsh {

const CharWidthPropertyList &getCharWidthPropertyList() {
  static CharWidthPropertyList table = {{
#define GEN_ENUM(E, S) {CharWidthProperty::E, S},
      EACH_CHAR_WIDTH_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
  }};
  return table;
}

unsigned int getGraphemeWidth(const CharWidthProperties &ps, const GraphemeScanner::Result &ret) {
  unsigned int width = 0;
  unsigned int flagSeqCount = 0;
  for (unsigned int i = 0; i < ret.codePointCount; i++) {
    auto codePoint = ret.codePoints[i];
    if (ps.replaceInvalid && codePoint < 0) {
      codePoint = UnicodeUtil::REPLACEMENT_CHAR_CODE;
    } else if (ret.breakProperties[i] == GraphemeBoundary::BreakProperty::Regional_Indicator) {
      flagSeqCount++;
    }
    int w = UnicodeUtil::width(codePoint, ps.eaw);
    if (w > 0) {
      width += w;
    }
  }
  if (flagSeqCount == 2) {
    return ps.flagSeqWidth;
  }
  if (width > 2 && ps.zwjSeqFallback) {
    return width;
  }
  return width < 2 ? width : 2;
}

ColumnLen getCharLen(StringRef ref, CharLenOp op, const CharWidthProperties &ps) {
  GraphemeScanner::Result ret;
  iterateGraphemeUntil(ref, op == CharLenOp::NEXT_CHAR ? 1 : static_cast<size_t>(-1),
                       [&ret](const GraphemeScanner::Result &scanned) { ret = scanned; });
  ColumnLen len = {
      .byteSize = static_cast<unsigned int>(ret.ref.size()),
      .colSize = 0,
  };
  if (ret.codePointCount > 0) {
    len.colSize = getGraphemeWidth(ps, ret);
  }
  return len;
}

ColumnLen getWordLen(StringRef ref, WordLenOp op, const CharWidthProperties &ps) {
  Utf8WordStream stream(ref.begin(), ref.end());
  Utf8WordScanner scanner(stream);
  while (scanner.hasNext()) {
    ref = scanner.next();
    if (op == WordLenOp::NEXT_WORD) {
      break;
    }
  }
  ColumnLen len = {
      .byteSize = static_cast<unsigned int>(ref.size()),
      .colSize = 0,
  };
  for (GraphemeScanner graphemeScanner(ref); graphemeScanner.hasNext();) {
    GraphemeScanner::Result ret;
    graphemeScanner.next(ret);
    len.colSize += getGraphemeWidth(ps, ret);
  }
  return len;
}

// ##############################
// ##     ANSIEscapeSeqMap     ##
// ##############################

ANSIEscapeSeqMap ANSIEscapeSeqMap::fromString(StringRef setting) {
  std::unordered_map<HighlightTokenClass, std::string> values;

  // comment=\033... keyword=...
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    // extract entry `comment=...`
    auto retPos = setting.find(" ", pos);
    auto entry = setting.slice(pos, retPos);
    pos = retPos != StringRef::npos ? retPos + 1 : retPos;

    retPos = entry.find('=');
    if (retPos == StringRef::npos) {
      continue; // skip invalid entry
    }
    auto element = entry.slice(0, retPos);
    auto escapeSeq = entry.substr(retPos + 1); // FIXME: check escape sequence format?

    for (auto &e : getHighlightTokenEntries()) {
      if (element == e.second && !escapeSeq.empty()) {
        values[e.first] = escapeSeq.toString();
        break;
      }
    }
  }

  ANSIEscapeSeqMap seqMap(std::move(values));
  return seqMap;
}

// ##########################
// ##     LineRenderer     ##
// ##########################

class TokenEmitterImpl : public TokenEmitter {
private:
  std::vector<std::pair<HighlightTokenClass, Token>> tokens;

public:
  explicit TokenEmitterImpl(StringRef source) : TokenEmitter(source) {}

  std::vector<std::pair<HighlightTokenClass, Token>> take() && { return std::move(this->tokens); }

private:
  void emit(HighlightTokenClass tokenClass, Token token) override {
    this->tokens.emplace_back(tokenClass, token);
  }
};

void LineRenderer::renderPrompt(const StringRef prompt) {
  for (StringRef::size_type pos = 0; pos != StringRef::npos;) {
    auto r = prompt.find('\x1b', pos);
    auto sub = prompt.slice(pos, r);
    this->renderLines(sub);
    if (r != StringRef::npos) {
      auto remain = prompt.substr(r);
      if (auto len = startsWithAnsiEscape(remain)) {
        this->output += remain.substr(0, len);
        pos = r + len;
      } else {
        this->renderLines(prompt.substr(r, 1)); // \x1b
        pos = r + 1;
      }
    } else {
      pos = r;
    }
  }
}

bool LineRenderer::renderScript(const StringRef source) {
  // for syntax highlight
  TokenEmitterImpl tokenEmitter(source);
  auto error = tokenEmitter.tokenizeAndEmit();
  auto lex = tokenEmitter.getLexerPtr();
  this->tokens = std::move(tokenEmitter).take();

  // render lines with highlight
  unsigned int curPos = 0;
  for (auto &e : this->tokens) {
    Token token = e.second;
    assert(curPos <= token.pos);
    this->render(source.slice(curPos, token.pos), HighlightTokenClass::NONE);
    curPos = token.endPos();
    this->render(source.substr(token.pos, token.size), e.first);
  }
  // render remain lines
  if (curPos < source.size()) {
    auto remain = source.substr(curPos);
    this->render(remain, HighlightTokenClass::NONE);
  }

  // line continuation checking
  if (error) {
    if (error->getTokenKind() == TokenKind::EOS) {
      return false;
    } else {
      auto kind = error->getTokenKind();
      if (isUnclosedToken(kind) && kind != TokenKind::UNCLOSED_REGEX_LITERAL) {
        return false;
      }
    }
  } else if (!this->tokens.empty()) {
    auto token = this->tokens.back().second;
    auto last = lex->toStrRef(token);
    switch (this->tokens.back().first) {
    case HighlightTokenClass::NONE:
      if (last.size() == 2 && last == "\\\n") {
        return false;
      }
      break;
    case HighlightTokenClass::COMMAND:
    case HighlightTokenClass::COMMAND_ARG:
      if (last.endsWith("\\\n")) {
        return false;
      }
      break;
    default:
      break;
    }
  }
  return true;
}

const std::string *LineRenderer::findColorCode(HighlightTokenClass tokenClass) const {
  if (this->escapeSeqMap) {
    auto iter = this->escapeSeqMap->getValues().find(tokenClass);
    if (iter != this->escapeSeqMap->getValues().end()) {
      return &(iter->second);
    }
  }
  return nullptr;
}

static size_t getNewlineOffset(const GraphemeScanner::Result &grapheme) {
  if (grapheme.codePointCount == 1 && grapheme.codePoints[0] == '\n') {
    return 1;
  }
  if (grapheme.codePointCount == 2 && grapheme.codePoints[1] == '\n') {
    return 2;
  }
  return 0;
}

void LineRenderer::render(StringRef ref, HighlightTokenClass tokenClass) {
  (void)this->ps;
  auto *colorCode = this->findColorCode(tokenClass);
  if (colorCode) {
    this->output += *colorCode;
  }
  iterateGrapheme(ref, [&](const GraphemeScanner::Result &grapheme) {
    if (auto offset = getNewlineOffset(grapheme)) {
      for (size_t i = 0; i < offset - 1; i++) {
        char buf[4];
        unsigned int len = UnicodeUtil::codePointToUtf8(grapheme.codePoints[i], buf);
        this->output.append(buf, len); // FIXME: quote control chars
      }
      if (colorCode) {
        this->output += "\x1b[0m";
      }
      this->output += "\r\n";
      this->output.append(this->initOffset, ' ');
      if (colorCode) {
        this->output += *colorCode;
      }
    } else { // FIXME: quote control chars
      this->output += grapheme.ref;
    }
  });
  if (colorCode) {
    this->output += "\x1b[0m";
  }
}

} // namespace ydsh