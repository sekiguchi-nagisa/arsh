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

#include <unistd.h>

#include "chars.h"
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

// ###########################
// ##     KeyCodeReader     ##
// ###########################

static ssize_t readCodePoint(int fd, char (&buf)[8], int &code) {
  ssize_t readSize = read(fd, &buf[0], 1);
  if (readSize <= 0) {
    return readSize;
  }
  unsigned int byteSize = UnicodeUtil::utf8ByteSize(buf[0]);
  if (byteSize < 1 || byteSize > 4) {
    return -1;
  } else if (byteSize > 1) {
    readSize = read(fd, &buf[1], byteSize - 1);
    if (readSize <= 0) {
      return readSize;
    }
  }
  return static_cast<ssize_t>(UnicodeUtil::utf8ToCodePoint(buf, std::size(buf), code));
}

#define READ_BYTE(b, bs)                                                                           \
  do {                                                                                             \
    if (read(this->fd, b + bs, 1) <= 0) {                                                          \
      goto END;                                                                                    \
    } else {                                                                                       \
      seqSize++;                                                                                   \
    }                                                                                              \
  } while (false)

ssize_t KeyCodeReader::fetch() {
  constexpr const char ESC = '\x1b';
  char buf[8];
  int code;
  ssize_t readSize = readCodePoint(this->fd, buf, code);
  if (readSize <= 0) {
    return readSize;
  }
  assert(readSize > 0 && readSize < 5);
  this->keycode.assign(buf, static_cast<size_t>(readSize));
  if (isEscapeChar(code)) {
    assert(readSize == 1);
    char seq[8];
    unsigned int seqSize = 0;
    READ_BYTE(seq, seqSize);
    if (seq[0] != '[' && seq[0] != 'O' && seq[0] != ESC) { // ESC ? sequence
      goto END;
    }

    READ_BYTE(seq, seqSize);
    if (seq[0] == '[') {                    // ESC [ sequence
      if (seq[1] >= '0' && seq[1] <= '9') { // ESC [ n x
        READ_BYTE(seq, seqSize);
        if ((seq[1] == '2' && seq[2] == '0') ||
            (seq[1] == '1' && seq[2] == ';')) { // ESC [200~ or ESC [1;3A
          READ_BYTE(seq, seqSize);
          READ_BYTE(seq, seqSize);
          goto END;
        }
      } else { // ESC [ x
        goto END;
      }
    } else if (seq[0] == 'O') { // ESC O sequence
      goto END;
    } else if (seq[0] == ESC) {
      if (seq[1] == '[') { // ESC ESC [ ? sequence
        READ_BYTE(seq, seqSize);
        goto END;
      }
    }
  END:
    this->keycode.append(seq, seqSize);
  }
  return static_cast<ssize_t>(this->keycode.size());
}

} // namespace ydsh