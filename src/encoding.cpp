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

#include "encoding.h"
#include "misc/grapheme.hpp"
#include "misc/word.hpp"

namespace ydsh {

const CharWidthPropertyList &getCharWidthPropertyList() {
  static CharWidthPropertyList table = {{
#define GEN_ENUM(E, S) {CharWidthProperty::E, S},
      EACH_CHAR_WIDTH_PROPERY(GEN_ENUM)
#undef GEN_ENUM
  }};
  return table;
}

static unsigned int graphemeWidth(const CharWidthProperties &ps,
                                  const GraphemeScanner::Result &ret) {
  const auto eaw = ps.fullWidth ? UnicodeUtil::FULL_WIDTH : UnicodeUtil::HALF_WIDTH;
  unsigned int width = 0;
  unsigned int flagSeqCount = 0;
  for (unsigned int i = 0; i < ret.codePointCount; i++) {
    int w = UnicodeUtil::width(ret.codePoints[i], eaw);
    if (ret.breakProperties[i] == GraphemeBoundary::BreakProperty::Regional_Indicator) {
      flagSeqCount++;
    }
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
  return width < 2 ? 1 : 2;
}

ColumnLen getCharLen(StringRef ref, CharLenOp op, const CharWidthProperties &ps) {
  GraphemeScanner scanner(ref);
  GraphemeScanner::Result ret;
  while (scanner.hasNext()) {
    scanner.next(ret);
    if (op == CharLenOp::NEXT_CHAR) {
      break;
    }
  }

  ColumnLen len = {
      .byteSize = static_cast<unsigned int>(ret.ref.size()),
      .colSize = 0,
  };
  if (ret.codePointCount > 0) {
    len.colSize = graphemeWidth(ps, ret);
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
    len.colSize += graphemeWidth(ps, ret);
  }
  return len;
}

} // namespace ydsh