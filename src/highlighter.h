/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef YDSH_HIGHLIGHTER_H
#define YDSH_HIGHLIGHTER_H

#include "highlighter_base.h"

namespace ydsh {

class ANSIEscapeSeqMap {
private:
  std::unordered_map<HighlightTokenClass, std::string> values;

public:
  static ANSIEscapeSeqMap fromString(StringRef setting);

  const auto &getValues() const { return this->values; }
};

class BuiltinHighlighter : public TokenEmitter, public ANSIFormatOp<BuiltinHighlighter> {
private:
  friend struct ANSIFormatOp<BuiltinHighlighter>;

  const ANSIEscapeSeqMap &escapeSeqMap;

  std::vector<std::pair<HighlightTokenClass, Token>> tokens;

  std::string buf;

public:
  BuiltinHighlighter(const ANSIEscapeSeqMap &seqMap, StringRef source)
      : TokenEmitter(source), escapeSeqMap(seqMap) {
    this->buf.reserve(this->source.size() * 2);
  }

  /**
   * entry point of syntax highlight
   * @return
   * if reach incomplete input (need more characters),
   * return false
   */
  bool doHighlight();

  std::string take() && { return std::move(this->buf); }

private:
  void emit(HighlightTokenClass tokenClass, Token token) override;

  void write(StringRef ref) { this->buf += ref; }

  void writeTrivia(StringRef ref);

  void write(StringRef ref, HighlightTokenClass tokenClass);
};

} // namespace ydsh

#endif // YDSH_HIGHLIGHTER_H
