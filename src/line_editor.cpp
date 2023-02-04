/*
 * Copyright (C) 2022 Nagisa Sekiguchi <s dot nagisa dot xyz at gmail dot com>
 * Copyright (c) 2010-2014, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * linenoise.c -- VERSION 1.0
 *
 * Guerrilla line editing library against the idea that a line editing lib
 * needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cursor position as ESC [ n ; m R
 *            where n is the row and m is the column
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * When linenoiseClearScreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "chars.h"
#include "line_editor.h"
#include "misc/buffer.hpp"
#include "vm.h"

// ++++++++++ copied from linenoise.c ++++++++++++++

enum {
  LINENOISE_MAX_LINE = 4096,
};
#define UNUSED(x) (void)(x)
static const char *unsupported_term[] = {"dumb", "cons25", "emacs", nullptr};

using NewlinePos = ydsh::FlexBuffer<unsigned int>;

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
  int ifd;                /* Terminal stdin file descriptor. */
  int ofd;                /* Terminal stdout file descriptor. */
  char *buf;              /* Edited line buffer. */
  size_t buflen;          /* Edited line buffer size. */
  ydsh::StringRef prompt; /* Prompt to display. */
  size_t pos;             /* Current cursor position. */
  size_t oldcolpos;       /* Previous refresh cursor column position. */
  size_t oldrow;          /* Previous refresh cursor row position. */
  size_t len;             /* Current edited line length. */
  size_t cols;            /* Number of columns in terminal. */
  size_t maxrows;         /* Maximum num of rows used so far (multiline mode) */
  ydsh::CharWidthProperties ps;
  NewlinePos newlinePos; // maintains newline pos
  bool rotating;

  ydsh::StringRef lineRef() const { return {this->buf, this->len}; }

  bool isSingleline() const { return this->newlinePos.empty(); }
};

enum KEY_ACTION {
  KEY_NULL = 0, /* NULL */
  ENTER = 13,   /* Enter */
  ESC = 27,     /* Escape */
};

/* Debugging macro. */
#if 0
FILE *lndebug_fp = nullptr;
#define lndebug(...)                                                                               \
  do {                                                                                             \
    if (lndebug_fp == nullptr) {                                                                   \
      lndebug_fp = fopen("/dev/pts/2", "a");                                                       \
    }                                                                                              \
    fprintf(lndebug_fp, "\n[%d %d %d %d] rows: %d, rpos: %d, max: %d, oldmax: %d\n", (int)l.len,   \
            (int)l.pos, (int)l.oldcolpos, (int)l.oldrow, rows, rpos, (int)l.maxrows, old_rows);    \
    fprintf(lndebug_fp, ", " __VA_ARGS__);                                                         \
    fflush(lndebug_fp);                                                                            \
  } while (0)
#else
#define lndebug(fmt, ...)
#endif

/* ========================== Encoding functions ============================= */

/* Get byte length and column length of the previous character */
static size_t prevCharLen(const ydsh::CharWidthProperties &ps, ydsh::StringRef bufRef, size_t pos,
                          size_t *col_len) {
  auto ref = bufRef.substr(0, pos);
  auto ret = ydsh::getCharLen(ref, ydsh::CharLenOp::PREV_CHAR, ps);
  if (col_len) {
    *col_len = ret.colSize;
  }
  return ret.byteSize;
}

/* Get byte length and column length of the next character */
static size_t nextCharLen(const ydsh::CharWidthProperties &ps, ydsh::StringRef bufRef, size_t pos,
                          size_t *col_len) {
  //  ydsh::StringRef ref(buf + pos, buf_len - pos);
  auto ref = bufRef.substr(pos);
  auto ret = ydsh::getCharLen(ref, ydsh::CharLenOp::NEXT_CHAR, ps);
  if (col_len) {
    *col_len = ret.colSize;
  }
  return ret.byteSize;
}

static size_t prevWordLen(const ydsh::CharWidthProperties &ps, ydsh::StringRef bufRef, size_t pos,
                          size_t *col_len) {
  auto ref = bufRef.substr(0, pos);
  auto ret = ydsh::getWordLen(ref, ydsh::WordLenOp::PREV_WORD, ps);
  if (col_len) {
    *col_len = ret.colSize;
  }
  return ret.byteSize;
}

static size_t nextWordLen(const ydsh::CharWidthProperties &ps, ydsh::StringRef bufRef, size_t pos,
                          size_t *col_len) {
  auto ref = bufRef.substr(pos);
  auto ret = ydsh::getWordLen(ref, ydsh::WordLenOp::NEXT_WORD, ps);
  if (col_len) {
    *col_len = ret.colSize;
  }
  return ret.byteSize;
}

/* Get column length from beginning of buffer to current byte position */
static size_t columnPos(const ydsh::CharWidthProperties &ps, ydsh::StringRef bufRef,
                        const size_t pos) {
  size_t ret = 0;
  for (size_t off = 0; off < pos;) {
    size_t col_len;
    size_t len = nextCharLen(ps, bufRef, off, &col_len);
    off += len;
    ret += col_len;
  }
  return ret;
}

/* Check if text is an ANSI escape sequence
 */
static bool isAnsiEscape(const char *buf, size_t buf_len, size_t *len) {
  if (buf_len > 2 && !memcmp("\033[", buf, 2)) {
    size_t off = 2;
    while (off < buf_len) {
      switch (buf[off++]) {
      case 'A':
      case 'B':
      case 'C':
      case 'D':
      case 'E':
      case 'F':
      case 'G':
      case 'H':
      case 'J':
      case 'K':
      case 'S':
      case 'T':
      case 'f':
      case 'm':
        *len = off;
        return true;
      }
    }
  }
  return false;
}

/* Get column length from beginning of buffer to current byte position for multiline mode*/
static size_t columnPosForMultiLine(const ydsh::CharWidthProperties &ps, ydsh::StringRef bufRef,
                                    const size_t pos, size_t cols, size_t iniPos) {
  assert(pos <= bufRef.size());

  size_t ret = 0;
  size_t colWidth = iniPos;
  for (size_t off = 0; off < pos;) {
    size_t colLen;
    size_t len = nextCharLen(ps, bufRef, off, &colLen);

    int dif = (int)(colWidth + colLen) - (int)cols;
    if (dif > 0) { // adjust pos for fullwidth character
      ret += (int)cols - (int)colWidth;
      colWidth = colLen;
    } else if (dif == 0) {
      colWidth = 0;
    } else {
      colWidth += colLen;
    }

    off += len;
    ret += colLen;
  }
  return ret;
}

/* ======================= Low level terminal handling ====================== */

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static bool isUnsupportedTerm() {
  char *term = getenv("TERM");
  if (term == nullptr) {
    return false;
  }
  for (int j = 0; unsupported_term[j]; j++) {
    if (!strcasecmp(term, unsupported_term[j])) {
      return true;
    }
  }
  return false;
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int getCursorPosition(int ifd, int ofd) {
  char buf[32];
  int cols, rows;

  /* Report cursor location */
  if (write(ofd, "\x1b[6n", 4) != 4) {
    return -1;
  }

  /* Read the response: ESC [ rows ; cols R */
  unsigned int i = 0;
  for (; i < sizeof(buf) - 1; i++) {
    if (read(ifd, buf + i, 1) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
  }
  buf[i] = '\0';

  /* Parse it. */
  if (buf[0] != ESC || buf[1] != '[') {
    return -1;
  }
  if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) {
    return -1;
  }
  return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int getColumns(int ifd, int ofd) {
  struct winsize ws; // NOLINT

  if (ioctl(ofd, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    /* ioctl() failed. Try to query the terminal itself. */
    int start, cols;

    /* Get the initial position, so we can restore it later. */
    start = getCursorPosition(ifd, ofd);
    if (start == -1) {
      goto failed;
    }

    /* Go to right margin and get position. */
    if (write(ofd, "\x1b[999C", 6) != 6) {
      goto failed;
    }
    cols = getCursorPosition(ifd, ofd);
    if (cols == -1) {
      goto failed;
    }

    /* Restore position. */
    if (cols > start) {
      char seq[32];
      snprintf(seq, 32, "\x1b[%dD", cols - start);
      if (write(ofd, seq, strlen(seq)) == -1) {
        /* Can't recover... */
      }
    }
    return cols;
  } else {
    return ws.ws_col;
  }

failed:
  return 80;
}

static void updateColumns(struct linenoiseState &ls) { ls.cols = getColumns(ls.ifd, ls.ofd); }

/* Clear the screen. Used to handle ctrl+l */
static void linenoiseClearScreen(int fd) {
  if (write(fd, "\x1b[H\x1b[2J", 7) <= 0) {
    /* nothing to do, just to avoid warning. */
  }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(int fd) {
  ssize_t r = write(fd, "\x07", strlen("\x07"));
  UNUSED(r);
  fsync(fd);
}

/* ============================== Completion ================================ */

#if 0
static FILE *logfp = nullptr;
#define logprintf(fmt, ...)                                                                        \
  do {                                                                                             \
    if (logfp == nullptr) {                                                                        \
      logfp = fopen("/dev/pts/4", "w");                                                            \
    }                                                                                              \
    fprintf(logfp, fmt, ##__VA_ARGS__);                                                            \
  } while (0)
#else
#define logprintf(fmt, ...)
#endif

static void showAllCandidates(const ydsh::CharWidthProperties &ps, int fd, size_t cols,
                              const ydsh::ArrayObject &candidates) {
  const auto len = candidates.size();
  auto *sizeTable = (unsigned int *)malloc(sizeof(unsigned int) * len);

  // compute maximum length of candidate
  size_t maxSize = 0;
  for (size_t index = 0; index < len; index++) {
    StringRef can = candidates.getValues()[index].asCStr(); // truncate characters after null
    size_t s = columnPos(ps, can, can.size());
    if (s > maxSize) {
      maxSize = s;
    }
    sizeTable[index] = s;
  }

  maxSize += 2;
  const unsigned int columCount = cols / maxSize;

  logprintf("cols: %lu\n", cols);
  logprintf("maxSize: %lu\n", maxSize);
  logprintf("columCount: %u\n", columCount);

  // compute raw size
  size_t rawSize;
  for (rawSize = 1; rawSize < len; rawSize++) {
    size_t a = len / rawSize;
    size_t b = len % rawSize;
    size_t c = b == 0 ? 0 : 1;
    if (a + c <= columCount) {
      break;
    }
  }

  logprintf("rawSize: %zu\n", rawSize);

  // show candidates
  ssize_t r = write(fd, "\r\n", strlen("\r\n"));
  UNUSED(r);
  for (size_t index = 0; index < rawSize; index++) {
    for (size_t j = 0;; j++) {
      size_t candidateIndex = j * rawSize + index;
      if (candidateIndex >= len) {
        break;
      }

      // print candidate
      auto c = candidates.getValues()[candidateIndex].asStrRef();
      r = write(fd, c.data(), c.size());
      UNUSED(r);

      // print spaces
      for (unsigned int s = 0; s < maxSize - sizeTable[candidateIndex]; s++) {
        r = write(fd, " ", 1);
        UNUSED(r);
      }
    }
    r = write(fd, "\r\n", strlen("\r\n"));
    UNUSED(r);
  }

  free(sizeTable);
}

static ydsh::StringRef getCommonPrefix(const ydsh::ArrayObject &candidates) {
  if (candidates.size() == 0) {
    return nullptr;
  }
  if (candidates.size() == 1) {
    return candidates.getValues()[0].asCStr(); // truncate chracters after null
  }

  size_t prefixSize;
  for (prefixSize = 0;; prefixSize++) {
    bool stop = false;
    const char ch = candidates.getValues()[0].asCStr()[prefixSize];
    for (size_t i = 1; i < candidates.size(); i++) {
      auto str = candidates.getValues()[i].asStrRef();
      if (str[0] == '\0' || prefixSize >= str.size() || ch != str.data()[prefixSize]) {
        stop = true;
        break;
      }
    }

    if (stop) {
      break;
    }
  }

  if (prefixSize == 0) {
    return nullptr;
  }
  return {candidates.getValues()[0].asCStr(), prefixSize};
}

/**
 * workaround for screen/tmux
 * @return
 */
static bool underMultiplexer() {
  StringRef env = getenv("TERM");
  if (env.contains("screen")) {
    return true;
  }
  if (getenv("TMUX")) {
    return true;
  }
  return false;
}

static void checkProperty(struct linenoiseState &l) {
  if (underMultiplexer()) {
    /**
     * if run under terminal multiplexer (screen/tmux), disabale character width checking
     */
    return;
  }

  for (auto &e : ydsh::getCharWidthPropertyList()) {
    const char *str = e.second;
    if (write(l.ofd, str, strlen(str)) == -1) {
      return;
    }
    int pos = getCursorPosition(l.ifd, l.ofd);
    /**
     * restore pos and clear line
     */
    const char *r = "\r\x1b[2K";
    if (write(l.ofd, r, strlen(r)) == -1) {
      return;
    }
    assert(pos > 0);
    l.ps.setProperty(e.first, pos - 1);
  }
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is a heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
  char *b{nullptr};
  size_t len{0};

  ~abuf() { free(this->b); }

  void append(const char *s, size_t slen) {
    char *buf = nullptr;
    if (this->len + slen > 0) {
      buf = (char *)realloc(this->b, this->len + slen);
    }
    if (buf == nullptr) {
      return;
    }
    memcpy(buf + this->len, s, slen);
    this->b = buf;
    this->len += slen;
  }
};

static void fillNewlinePos(NewlinePos &newlinePos, StringRef ref) {
  newlinePos.clear();
  for (StringRef::size_type pos = 0;;) {
    auto retPos = ref.find('\n', pos);
    if (retPos != StringRef::npos) {
      newlinePos.push_back(retPos);
      pos = retPos + 1;
    } else {
      break;
    }
  }
}

static unsigned int findCurIndex(const NewlinePos &newlinePos, unsigned int pos) {
  auto iter = std::lower_bound(newlinePos.begin(), newlinePos.end(), pos);
  if (iter == newlinePos.end()) {
    return newlinePos.size();
  }
  return iter - newlinePos.begin();
}

/* Move cursor on the left. */
static bool linenoiseEditMoveLeft(struct linenoiseState &l) {
  if (l.pos > 0) {
    l.pos -= prevCharLen(l.ps, l.lineRef(), l.pos, nullptr);
    return true;
  }
  return false;
}

/* Move cursor on the right. */
static bool linenoiseEditMoveRight(struct linenoiseState &l) {
  if (l.pos != l.len) {
    l.pos += nextCharLen(l.ps, l.lineRef(), l.pos, nullptr);
    return true;
  }
  return false;
}

/* Move cursor to the start of the line. */
static bool linenoiseEditMoveHome(struct linenoiseState &l) {
  unsigned int newPos = 0;
  if (l.isSingleline()) { // single-line
    newPos = 0;
  } else { // multi-line
    unsigned int index = findCurIndex(l.newlinePos, l.pos);
    if (index == 0) {
      newPos = 0;
    } else {
      newPos = l.newlinePos[index - 1] + 1;
    }
  }
  if (l.pos != newPos) {
    l.pos = newPos;
    return true;
  }
  return false;
}

/* Move cursor to the end of the line. */
static bool linenoiseEditMoveEnd(struct linenoiseState &l) {
  unsigned int newPos = 0;
  if (l.isSingleline()) { // single-line
    newPos = l.len;
  } else { // multi-line
    unsigned int index = findCurIndex(l.newlinePos, l.pos);
    if (index == l.newlinePos.size()) {
      newPos = l.len;
    } else {
      newPos = l.newlinePos[index];
    }
  }
  if (l.pos != newPos) {
    l.pos = newPos;
    return true;
  }
  return false;
}

static bool linenoiseEditMoveLeftWord(struct linenoiseState &l) {
  if (l.pos > 0) {
    l.pos -= prevWordLen(l.ps, l.lineRef(), l.pos, nullptr);
    return true;
  }
  return false;
}

static bool linenoiseEditMoveRightWord(struct linenoiseState &l) {
  if (l.pos != l.len) {
    l.pos += nextWordLen(l.ps, l.lineRef(), l.pos, nullptr);
    return true;
  }
  return false;
}

static void revertInsert(struct linenoiseState &l, size_t len) {
  if (len > 0) {
    memmove(l.buf + l.pos - len, l.buf + l.pos, l.len - l.pos);
    l.pos -= len;
    l.len -= len;
    l.buf[l.len] = '\0';
  }
}

static std::pair<unsigned int, unsigned int> findLineInterval(const struct linenoiseState &l,
                                                              bool wholeLine) {
  unsigned int pos = 0;
  unsigned int len = l.len;
  if (l.isSingleline()) { // single-line
    pos = 0;
    len = (wholeLine ? l.len : l.pos);
  } else { // multi-line
    unsigned int index = findCurIndex(l.newlinePos, l.pos);
    if (index == 0) {
      pos = 0;
      len = wholeLine ? l.newlinePos[index] : l.pos;
    } else if (index < l.newlinePos.size()) {
      pos = l.newlinePos[index - 1] + 1;
      len = (wholeLine ? l.newlinePos[index] : l.pos) - pos;
    } else {
      pos = l.newlinePos[index - 1] + 1;
      len = (wholeLine ? l.len : l.pos) - pos;
    }
  }
  return {pos, len};
}

static void linenoiseEditDeleteTo(struct linenoiseState &l, bool wholeLine = false) {
  auto [newPos, delLen] = findLineInterval(l, wholeLine);
  l.pos = newPos + delLen;
  revertInsert(l, delLen);
}

static void linenoiseEditDeleteFrom(struct linenoiseState &l) {
  if (l.isSingleline()) { // single-line
    l.buf[l.pos] = '\0';
    l.len = l.pos;
  } else { // multi-line
    unsigned int index = findCurIndex(l.newlinePos, l.pos);
    unsigned int newPos = 0;
    if (index == l.newlinePos.size()) {
      newPos = l.len;
    } else {
      newPos = l.newlinePos[index];
    }
    unsigned int delLen = newPos - l.pos;
    l.pos = newPos;
    revertInsert(l, delLen);
  }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
static bool linenoiseEditDelete(struct linenoiseState &l) {
  if (l.len > 0 && l.pos < l.len) {
    size_t chlen = nextCharLen(l.ps, l.lineRef(), l.pos, nullptr);
    memmove(l.buf + l.pos, l.buf + l.pos + chlen, l.len - l.pos - chlen);
    l.len -= chlen;
    l.buf[l.len] = '\0';
    return true;
  }
  return false;
}

/* Backspace implementation. */
static bool linenoiseEditBackspace(struct linenoiseState &l, std::string *cutBuf = nullptr) {
  if (l.pos > 0 && l.len > 0) {
    size_t chlen = prevCharLen(l.ps, l.lineRef(), l.pos, nullptr);
    if (cutBuf) {
      *cutBuf = std::string(l.buf + l.pos - chlen, chlen);
    }
    memmove(l.buf + l.pos - chlen, l.buf + l.pos, l.len - l.pos);
    l.pos -= chlen;
    l.len -= chlen;
    l.buf[l.len] = '\0';
    return true;
  }
  return false;
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. */
static bool linenoiseEditDeletePrevWord(struct linenoiseState &l) {
  if (l.pos > 0 && l.len > 0) {
    size_t wordLen = prevWordLen(l.ps, l.lineRef(), l.pos, nullptr);
    memmove(l.buf + l.pos - wordLen, l.buf + l.pos, l.len - l.pos);
    l.pos -= wordLen;
    l.len -= wordLen;
    l.buf[l.len] = '\0';
    return true;
  }
  return false;
}

static bool linenoiseEditDeleteNextWord(struct linenoiseState &l) {
  if (l.len > 0 && l.pos < l.len) {
    size_t wordLen = nextWordLen(l.ps, l.lineRef(), l.pos, nullptr);
    memmove(l.buf + l.pos, l.buf + l.pos + wordLen, l.len - l.pos - wordLen);
    l.len -= wordLen;
    l.buf[l.len] = '\0';
    return true;
  }
  return false;
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal false is returned, otherwise true. */
static bool linenoiseEditInsert(struct linenoiseState &l, const char *cbuf, size_t clen) {
  if (l.len + clen <= l.buflen) {
    if (l.len == l.pos) {
      memcpy(&l.buf[l.pos], cbuf, clen);
      l.pos += clen;
      l.len += clen;
      l.buf[l.len] = '\0';
    } else {
      memmove(l.buf + l.pos + clen, l.buf + l.pos, l.len - l.pos);
      memcpy(&l.buf[l.pos], cbuf, clen);
      l.pos += clen;
      l.len += clen;
      l.buf[l.len] = '\0';
    }
    return true;
  }
  return false;
}

static bool linenoiseEditSwapChars(struct linenoiseState &l) {
  if (l.pos == 0) { //  does not swap
    return false;
  } else if (l.pos == l.len) {
    linenoiseEditMoveLeft(l);
  }
  std::string cutStr;
  return linenoiseEditBackspace(l, &cutStr) && linenoiseEditMoveRight(l) &&
         linenoiseEditInsert(l, cutStr.c_str(), cutStr.size());
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *linenoiseNoTTY(int inFd) {
  char *line = nullptr;
  size_t len = 0, maxlen = 0;

  while (true) {
    if (len == maxlen) {
      if (maxlen == 0) {
        maxlen = 16;
      }
      maxlen *= 2;
      char *oldval = line;
      line = (char *)realloc(line, maxlen);
      if (line == nullptr) {
        if (oldval) {
          free(oldval);
        }
        return nullptr;
      }
    }
    signed char c;
    if (read(inFd, &c, 1) <= 0) {
      c = EOF;
    }
    if (c == EOF || c == '\n') {
      if (c == EOF && len == 0) {
        free(line);
        return nullptr;
      } else {
        line[len] = '\0';
        return line;
      }
    } else {
      line[len] = c;
      len++;
    }
  }
}

// +++++++++++++++++++++++++++++++++++++++++++++++++++

namespace ydsh {

// ##############################
// ##     LineEditorObject     ##
// ##############################

LineEditorObject::LineEditorObject() : ObjectWithRtti(TYPE::LineEditor) {
  this->inFd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
  this->outFd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
}

LineEditorObject::~LineEditorObject() {
  close(this->inFd);
  close(this->outFd);
}

/* Raw mode: 1960 magic shit. */
int LineEditorObject::enableRawMode(int fd) {
  struct termios raw; // NOLINT

  if (!isatty(fd)) {
    goto fatal;
  }
  if (tcgetattr(fd, &this->orgTermios) == -1) {
    goto fatal;
  }

  raw = this->orgTermios; /* modify the original mode */
  /* input modes: no break, no CR to NL, no parity check, no strip char
   */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP);
  /* output modes - disable post processing */
  //    raw.c_oflag &= ~(OPOST);
  /* control modes - set 8 bit chars */
  raw.c_cflag |= (CS8);
  /* local modes - choing off, canonical off, no extended functions,
   * no signal chars (^Z,^C) */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  /* control chars - set return condition: min number of bytes and timer.
   * We want read to return every single byte, without timeout. */
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

  /* put terminal in raw mode after flushing */
  if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) {
    goto fatal;
  }
  this->rawMode = true;
  return 0;

fatal:
  errno = ENOTTY;
  return -1;
}

void LineEditorObject::disableRawMode(int fd) {
  /* Don't even check the return value as it's too late. */
  if (this->rawMode && tcsetattr(fd, TCSAFLUSH, &this->orgTermios) != -1) {
    this->rawMode = false;
  }
}

/**
 * if current cursor is not head of line. write % symbol like zsh
 * @param l
 */
static int preparePrompt(struct linenoiseState &l) {
  if (getCursorPosition(l.ifd, l.ofd) > 1) {
    const char *s = "\x1b[7m%\x1b[0m\r\n";
    if (write(l.ofd, s, strlen(s)) == -1) {
      return -1;
    }
  }

  // enable bracket paste mode
  {
    const char *s = "\x1b[?2004h";
    if (write(l.ofd, s, strlen(s)) == -1) {
      return -1;
    }
  }

  checkProperty(l);
  return 0;
}

static std::pair<size_t, size_t> getColRowLen(const CharWidthProperties &ps, const StringRef line,
                                              const size_t cols, bool isPrompt,
                                              const size_t initPos) {
  size_t col = 0;
  size_t row = 0;

  for (StringRef::size_type pos = 0;;) {
    auto retPos = line.find('\n', pos);
    auto sub = line.slice(pos, retPos);

    char buf[LINENOISE_MAX_LINE];
    size_t bufLen = 0;
    for (size_t off = 0; off < sub.size();) {
      if (size_t len; isPrompt && isAnsiEscape(sub.data() + off, sub.size() - off, &len)) {
        off += len;
        continue;
      }
      buf[bufLen++] = sub[off++];
    }

    auto colLen = columnPosForMultiLine(ps, StringRef(buf, bufLen), bufLen, cols, initPos);
    if (retPos == StringRef::npos) {
      if (isPrompt) {
        col = colLen % cols;
        row += colLen / cols;
      } else {
        col = colLen;
      }
      break;
    } else {
      col = colLen % cols;
      row += (initPos + colLen) / cols;
      row++;
      pos = retPos + 1;
    }
  }
  return {col, row};
}

static std::pair<size_t, size_t> getPromptColRow(const CharWidthProperties &ps,
                                                 const StringRef prompt, const size_t cols) {
  return getColRowLen(ps, prompt, cols, true, 0);
}

static void appendLines(abuf &buf, const StringRef ref, size_t initCols) {
  for (StringRef::size_type pos = 0;;) {
    const auto retPos = ref.find('\n', pos);
    auto sub = ref.slice(pos, retPos);
    buf.append(sub.data(), sub.size());
    if (retPos != StringRef::npos) {
      buf.append("\r\n", 2);
      for (size_t i = 0; i < initCols; i++) {
        buf.append(" ", 1);
      }
      pos = retPos + 1;
    } else {
      break;
    }
  }
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
void LineEditorObject::refreshLine(struct linenoiseState &l, bool doHighlight) {
  updateColumns(l);

  char seq[64];
  const auto [pcollen, prow] = getPromptColRow(l.ps, l.prompt, l.cols);
  const auto [colpos, row] = getColRowLen(l.ps, l.lineRef(), l.cols, false, pcollen);
  /* rows used by current buf. */
  int rows = (pcollen + colpos + l.cols - 1) / l.cols + prow + row;
  /* cursor relative row. */
  int rpos = (pcollen + l.oldcolpos + l.cols) / l.cols + prow + l.oldrow;
  int old_rows = l.maxrows;
  struct abuf ab;

  lndebug("cols: %d, pcolloen: %d, prow: %d, colpos: %d, row: %d", (int)l.cols, (int)pcollen,
          (int)prow, (int)colpos, (int)row);

  /* Update maxrows if needed. */
  if (rows > (int)l.maxrows) {
    l.maxrows = rows;
  }

  /* First step: clear all the lines used before. To do so start by
   * going to the last row. */
  if (old_rows - rpos > 0) {
    lndebug("go down %d", old_rows - rpos);
    snprintf(seq, 64, "\x1b[%dB", old_rows - rpos);
    ab.append(seq, strlen(seq));
  }

  /* Now for every row clear it, go up. */
  for (int j = 0; j < old_rows - 1; j++) {
    lndebug("clear+up");
    snprintf(seq, 64, "\r\x1b[0K\x1b[1A");
    ab.append(seq, strlen(seq));
  }

  /* Clean the top line. */
  lndebug("clear");
  snprintf(seq, 64, "\r\x1b[0K");
  ab.append(seq, strlen(seq));

  /* Write the prompt and the current buffer content */
  appendLines(ab, l.prompt, 0);
  if (StringRef lineRef = l.lineRef(); !lineRef.empty()) {
    if (this->highlight) {
      if (doHighlight || this->highlightCache.empty()) {
        std::string line = lineRef.toString();
        line += '\n';
        BuiltinHighlighter highlighter(this->escapeSeqMap, line);
        this->continueLine = !highlighter.doHighlight();
        line = std::move(highlighter).take();
        if (!line.empty() && line.back() == '\n') {
          line.pop_back();
        }
        this->highlightCache = std::move(line);
      }
      lineRef = this->highlightCache;
    }
    fillNewlinePos(l.newlinePos, l.lineRef());
    appendLines(ab, lineRef, pcollen);
  }

  /* Get column length to cursor position */
  const auto [colpos2, row2] =
      getColRowLen(l.ps, l.lineRef().slice(0, l.pos), l.cols, false, pcollen);

  /* If we are at the very end of the screen with our prompt, we need to
   * emit a newline and move the prompt to the first column. */
  if (l.pos && l.pos == l.len && (colpos2 + pcollen) % l.cols == 0) { // FIXME: support multiline?
    lndebug("<newline>");
    ab.append("\n", 1);
    snprintf(seq, 64, "\r");
    ab.append(seq, strlen(seq));
    rows++;
    if (rows > (int)l.maxrows) {
      l.maxrows = rows;
    }
  }

  /* Move cursor to right row position. */
  int rpos2 =
      (pcollen + colpos2 + l.cols) / l.cols + prow + row2; /* current cursor relative row. */
  lndebug("rpos2 %d", rpos2);

  /* Go up till we reach the expected position. */
  if (rows - rpos2 > 0) {
    lndebug("go-up %d", rows - rpos2);
    snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
    ab.append(seq, strlen(seq));
  }

  /* Set column position, zero-based. */
  int col = (pcollen + colpos2) % l.cols;
  lndebug("set col %d", 1 + col);
  if (col) {
    snprintf(seq, 64, "\r\x1b[%dC", col);
  } else {
    snprintf(seq, 64, "\r");
  }
  ab.append(seq, strlen(seq));

  lndebug("\n");
  l.oldcolpos = colpos2;
  l.oldrow = row2;

  if (write(l.ofd, ab.b, ab.len) == -1) {
  } /* Can't recover from write error. */
}

static bool insertBracketPaste(struct linenoiseState &l) {
  while (true) {
    char buf;
    if (read(l.ifd, &buf, 1) <= 0) {
      return false;
    }
    switch (buf) {
    case KEY_NULL:
      continue; // ignore null character
    case ENTER:
      if (!linenoiseEditInsert(l, "\n", 1)) { // insert \n instead of \r
        return false;
      }
      continue;
    case ESC: { // bracket stop \e[201~
      char seq[6];
      seq[0] = '\x1b';
      const char expect[] = {'[', '2', '0', '1', '~'};
      unsigned int count = 0;
      for (; count < std::size(expect); count++) {
        if (read(l.ifd, seq + count + 1, 1) <= 0) {
          return false;
        }
        if (seq[count + 1] != expect[count]) {
          if (!linenoiseEditInsert(l, seq, count + 2)) {
            return false;
          }
          break;
        }
      }
      if (count == std::size(expect)) {
        return true; // end bracket paste mode
      }
      continue;
    }
    default:
      if (!linenoiseEditInsert(l, &buf, 1)) {
        return false;
      }
      continue;
    }
  }
  return true;
}

int LineEditorObject::accept(DSState &state, struct linenoiseState &l) {
  l.newlinePos.clear(); // force move cursor to end (force enter single line mode)
  if (linenoiseEditMoveEnd(l)) {
    this->refreshLine(l, false);
  }
  this->kickHistSyncCallback(state, l);
  if (state.hasError()) {
    errno = EAGAIN;
    return -1;
  }
  return static_cast<int>(l.len);
}

enum class HistRotateOp {
  PREV,
  NEXT,
};

class HistRotate {
private:
  std::unordered_map<unsigned int, DSValue> oldEntries;
  ObjPtr<ArrayObject> history;
  int histIndex{0};

public:
  explicit HistRotate(ObjPtr<ArrayObject> history) : history(std::move(history)) {
    if (this->history) {
      this->truncateUntilLimit();
      this->history->append(DSValue::createStr());
    }
  }

  ~HistRotate() { this->revertAll(); }

  void revertAll() {
    if (this->history) {
      if (this->history->size() > 0) {
        this->history->refValues().pop_back();
      }
      this->truncateUntilLimit();
      for (auto &e : this->oldEntries) { // revert modified entry
        if (e.first < this->history->size()) {
          this->history->refValues()[e.first] = std::move(e.second);
        }
      }
      this->history = nullptr;
    }
  }

  explicit operator bool() { return this->history && this->history->size() > 0; }

  /**
   * save current buffer and get next entry
   * @param curLine
   * @param next
   */
  bool rotate(StringRef &curBuf, HistRotateOp op) {
    this->truncateUntilLimit();

    // save current buffer content to current history entry
    auto histSize = static_cast<ssize_t>(this->history->size());
    ssize_t bufIndex = histSize - 1 - this->histIndex;
    if (!this->save(bufIndex, curBuf)) {
      this->histIndex = 0; // reset index
      return false;
    }

    this->histIndex += op == HistRotateOp::PREV ? 1 : -1;
    if (this->histIndex < 0) {
      this->histIndex = 0;
      return false;
    } else if (this->histIndex >= histSize) {
      this->histIndex = static_cast<int>(histSize) - 1;
      return false;
    } else {
      bufIndex = histSize - 1 - this->histIndex;
      curBuf = this->history->getValues()[bufIndex].asStrRef();
      return true;
    }
  }

  void truncateUntilLimit() {
    static_assert(SYS_LIMIT_HIST_SIZE < SYS_LIMIT_ARRAY_MAX);
    static_assert(SYS_LIMIT_HIST_SIZE < std::numeric_limits<ssize_t>::max());
    if (this->history->size() >= SYS_LIMIT_HIST_SIZE) {
      auto &values = this->history->refValues();
      values.erase(values.begin(),
                   values.begin() + static_cast<ssize_t>(values.size() - SYS_LIMIT_HIST_SIZE - 1));
      assert(values.size() == SYS_LIMIT_HIST_SIZE - 1);
    }
  }

private:
  bool save(ssize_t index, StringRef curBuf) {
    if (index < static_cast<ssize_t>(this->history->size()) && index > -1) {
      auto actualIndex = static_cast<unsigned int>(index);
      auto org = this->history->getValues()[actualIndex];
      this->oldEntries.emplace(actualIndex, std::move(org));
      this->history->refValues()[actualIndex] = DSValue::createStr(curBuf);
      return true;
    }
    return false;
  }
};

static bool rotateHistory(HistRotate &histRotate, struct linenoiseState &l, HistRotateOp op,
                          bool multiline) {
  if (!histRotate) {
    return false;
  }
  multiline = multiline && !l.isSingleline();

  auto curBuf = l.lineRef();
  if (multiline) {
    auto [pos, len] = findLineInterval(l, true);
    curBuf = curBuf.substr(pos, len);
  }

  if (!histRotate.rotate(curBuf, op)) {
    return false;
  }
  if (multiline) {
    linenoiseEditDeleteTo(l, true);
  } else {
    l.len = l.pos = 0;
  }
  const char *ptr = curBuf.data();
  return linenoiseEditInsert(l, ptr, strlen(ptr));
}

static bool rotateHistoryOrUpDown(HistRotate &histRotate, struct linenoiseState &l, HistRotateOp op,
                                  bool continueRotate) {
  if (l.isSingleline() || continueRotate) {
    l.rotating = true;
    return rotateHistory(histRotate, l, op, false);
  } else if (op == HistRotateOp::PREV || op == HistRotateOp::NEXT) { // move cursor up/down
    // resolve dest line
    const auto oldPos = l.pos;
    if (op == HistRotateOp::PREV) { // up
      linenoiseEditMoveHome(l);
      if (l.pos == 0) {
        l.pos = oldPos;
        return false;
      }
      l.pos--;
    } else { // down
      linenoiseEditMoveEnd(l);
      if (l.pos == l.len) {
        l.pos = oldPos;
        return false;
      }
      l.pos++;
    }
    StringRef dest;
    {
      auto [pos, len] = findLineInterval(l, true);
      dest = StringRef(l.buf + pos, len);
      l.pos = oldPos;
    }

    // resolve line to current position
    size_t count = 0;
    {
      auto [pos, len] = findLineInterval(l, false);
      auto line = StringRef(l.buf + pos, len);
      count = iterateGrapheme(line, [](const GraphemeScanner::Result &) {});
    }

    GraphemeScanner::Result ret;
    size_t retCount = iterateGraphemeUntil(
        dest, count, [&ret](const GraphemeScanner::Result &scanned) { ret = scanned; });
    if (retCount) {
      l.pos = ret.ref.end() - l.buf;
    } else {
      l.pos = dest.begin() - l.buf;
    }
    return true;
  }
  return false;
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
int LineEditorObject::editLine(DSState &state, char *buf, size_t buflen, const char *prompt) {
  if (this->enableRawMode(this->inFd)) {
    return -1;
  }

  /* Populate the linenoise state that we pass to functions implementing
   * specific editing functionalities. */
  struct linenoiseState l = {
      .ifd = this->inFd,
      .ofd = this->outFd,
      .buf = buf,
      .buflen = buflen,
      .prompt = prompt,
      .pos = 0,
      .oldcolpos = 0,
      .oldrow = 0,
      .len = 0,
      .cols = static_cast<size_t>(getColumns(this->inFd, this->outFd)),
      .maxrows = 0,
      .ps = {},
      .newlinePos = {},
      .rotating = false,
  };

  /* Buffer starts empty. */
  l.buf[0] = '\0';
  l.buflen--; /* Make sure there is always space for the null-term */

  const int count = this->editInRawMode(state, l);
  const int errNum = errno;
  if (count == -1 && errNum == EAGAIN) {
    l.newlinePos.clear(); // force move cursor to end (force enter single line mode)
    if (linenoiseEditMoveEnd(l)) {
      this->refreshLine(l, false);
    }
  }
  this->disableRawMode(this->inFd);
  ssize_t r = write(this->outFd, "\n", 1);
  UNUSED(r);
  return count;
}

int LineEditorObject::editInRawMode(DSState &state, struct linenoiseState &l) {
  /* The latest history entry is always our current buffer, that
   * initially is just an empty string. */
  HistRotate histRotate(this->history);

  preparePrompt(l);
  this->refreshLine(l);

  KeyCodeReader reader(l.ifd);
  while (true) {
    if (reader.fetch() <= 0) {
      return static_cast<int>(l.len);
    }

  NO_FETCH:
    const bool prevRotating = l.rotating;
    l.rotating = false;

    if (!reader.hasControlChar()) {
      auto &buf = reader.get();
      if (linenoiseEditInsert(l, buf.c_str(), buf.size())) {
        this->refreshLine(l);
        continue;
      } else {
        return -1;
      }
    }

    // dispatch edit action
    const auto *action = this->keyBindings.findAction(reader.get());
    if (!action) {
      continue; // skip unbound key action
    }

    switch (action->type) {
    case EditActionType::ACCEPT:
      if (this->continueLine) {
        if (linenoiseEditInsert(l, "\n", 1)) {
          this->refreshLine(l);
        } else {
          return -1;
        }
      } else {
        histRotate.revertAll();
        return this->accept(state, l);
      }
      break;
    case EditActionType::CANCEL:
      errno = EAGAIN;
      return -1;
    case EditActionType::COMPLETE:
      if (this->completionCallback) {
        auto s = this->completeLine(state, l, reader);
        if (s == CompStatus::ERROR) {
          return static_cast<int>(l.len);
        } else if (s == CompStatus::CANCEL) {
          errno = EAGAIN;
          return -1;
        }
        if (!reader.empty()) {
          goto NO_FETCH;
        }
      }
      break;
    case EditActionType::BACKWARD_DELETE_CHAR:
      if (linenoiseEditBackspace(l)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::DELETE_CHAR:
      if (linenoiseEditDelete(l)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::DELETE_OR_EXIT: /* remove char at right of cursor, or if the line is empty,
                                        act as end-of-file. */
      if (l.len > 0) {
        if (linenoiseEditDelete(l)) {
          this->refreshLine(l);
        }
      } else {
        errno = 0;
        return -1;
      }
      break;
    case EditActionType::TRANSPOSE_CHAR: /* swaps current character with previous */
      if (linenoiseEditSwapChars(l)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::BACKWARD_CHAR:
      if (linenoiseEditMoveLeft(l)) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::FORWARD_CHAR:
      if (linenoiseEditMoveRight(l)) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::PREV_HISTORY:
    case EditActionType::NEXT_HISTORY: {
      auto op =
          action->type == EditActionType::PREV_HISTORY ? HistRotateOp::PREV : HistRotateOp::NEXT;
      if (rotateHistory(histRotate, l, op, true)) {
        this->refreshLine(l);
      }
      break;
    }
    case EditActionType::UP_OR_HISTORY:
    case EditActionType::DOWN_OR_HISTORY: {
      auto op =
          action->type == EditActionType::UP_OR_HISTORY ? HistRotateOp::PREV : HistRotateOp::NEXT;
      if (rotateHistoryOrUpDown(histRotate, l, op, prevRotating)) {
        this->refreshLine(l);
      }
      break;
    }
    case EditActionType::BACKWORD_KILL_LINE: /* delete the whole line or delete to current */
      linenoiseEditDeleteTo(l);
      this->refreshLine(l);
      break;
    case EditActionType::KILL_LINE: /* delete from current to end of line */
      linenoiseEditDeleteFrom(l);
      this->refreshLine(l);
      break;
    case EditActionType::BEGINNING_OF_LINE: /* go to the start of the line */
      if (linenoiseEditMoveHome(l)) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::END_OF_LINE: /* go to the end of the line */
      if (linenoiseEditMoveEnd(l)) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::CLEAR_SCREEN:
      linenoiseClearScreen(l.ofd);
      this->refreshLine(l);
      break;
    case EditActionType::BACKWARD_KILL_WORD:
      if (linenoiseEditDeletePrevWord(l)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::KILL_WORD:
      if (linenoiseEditDeleteNextWord(l)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::BACKWARD_WORD:
      if (linenoiseEditMoveLeftWord(l)) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::FORWARD_WORD:
      if (linenoiseEditMoveRightWord(l)) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::NEWLINE:
      if (linenoiseEditInsert(l, "\n", 1)) {
        this->refreshLine(l);
      } else {
        return -1;
      }
      break;
    case EditActionType::BRACKET_PASTE:
      if (insertBracketPaste(l)) {
        this->refreshLine(l);
      } else {
        return -1;
      }
      break;
    case EditActionType::CUSTOM:
      if (this->kickCustomCallback(state, l, action->customActionType, action->customActionIndex)) {
        this->refreshLine(l);
        if (action->customActionType == CustomActionType::REPLACE_WHOLE_ACCEPT) {
          histRotate.revertAll();
          return this->accept(state, l);
        }
      } else {
        if (state.hasError()) {
          errno = EAGAIN;
        }
        return -1;
      }
      break;
    }
  }
  return static_cast<int>(l.len);
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *LineEditorObject::readline(DSState &state, StringRef promptRef) {
  errno = 0;
  if (!isatty(this->inFd)) {
    /* Not a tty: read from file / pipe. In this mode we don't want any
     * limit to the line size, so we call a function to handle that. */
    return linenoiseNoTTY(this->inFd);
  }

  this->lock = true;
  this->continueLine = false;
  this->highlightCache.clear();
  auto cleanup = finally([&] { this->lock = false; });

  // prepare prompt
  DSValue promptVal;
  if (this->promptCallback) {
    auto args = makeArgs(DSValue::createStr(promptRef));
    DSValue callback = this->promptCallback;
    promptVal = this->kickCallback(state, std::move(callback), std::move(args));
    if (state.hasError()) {
      errno = EAGAIN;
      return nullptr;
    }
  }
  if (promptVal.hasStrRef()) {
    promptRef = promptVal.asStrRef();
  }
  const char *prompt = promptRef.data(); // force truncate characters after null
  char buf[LINENOISE_MAX_LINE];
  if (isUnsupportedTerm()) {
    ssize_t r = write(this->outFd, prompt, strlen(prompt));
    UNUSED(r);
    fsync(this->outFd);
    ssize_t rlen = read(this->inFd, buf, LINENOISE_MAX_LINE);
    if (rlen <= 0) {
      return nullptr;
    }
    buf[rlen < LINENOISE_MAX_LINE ? rlen : rlen - 1] = '\0';
    size_t len = rlen < LINENOISE_MAX_LINE ? rlen : rlen - 1;
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      len--;
      buf[len] = '\0';
    }
    return strdup(buf);
  } else {
    int count = this->editLine(state, buf, LINENOISE_MAX_LINE, prompt);
    if (count == -1) {
      return nullptr;
    }
    return strdup(buf);
  }
}

/**
 * @param ls
 * @param candidates
 * @return
 * return token start cursor
 */
size_t LineEditorObject::insertEstimatedSuffix(struct linenoiseState &ls,
                                               const ArrayObject &candidates) {
  const auto prefix = getCommonPrefix(candidates);
  if (prefix.empty()) {
    return ls.pos;
  }

  logprintf("#prefix: %s\n", prefix.toString().c_str());
  logprintf("pos: %ld\n", ls.pos);

  // compute suffix
  bool matched = false;
  size_t offset = 0;
  if (ls.pos > 0) {
    for (offset = ls.pos - std::min(ls.pos, prefix.size()); offset < ls.pos; offset++) {
      StringRef suffix(ls.buf + offset, ls.pos - offset);
      logprintf("curSuffix: %s\n", suffix.toString().c_str());
      if (auto retPos = prefix.find(suffix); retPos == 0) {
        matched = true;
        break;
      }
    }
  }

  logprintf("offset: %ld\n", offset);
  if (matched) {
    size_t insertingSize = prefix.size() - (ls.pos - offset);
    StringRef inserting(prefix.data() + (prefix.size() - insertingSize), insertingSize);
    logprintf("inserting: %s\n", inserting.toString().c_str());
    if (linenoiseEditInsert(ls, inserting.data(), inserting.size())) {
      this->refreshLine(ls);
    }
  } else if (candidates.size() == 1) { // if candidate does not match previous token, insert it.
    if (linenoiseEditInsert(ls, prefix.data(), prefix.size())) {
      this->refreshLine(ls);
    }
  }
  return matched ? offset : ls.pos;
}

LineEditorObject::CompStatus
LineEditorObject::completeLine(DSState &state, struct linenoiseState &ls, KeyCodeReader &reader) {
  reader.clear();

  StringRef line(ls.buf, ls.pos);
  auto candidates = this->kickCompletionCallback(state, line);
  if (!candidates) {
    return CompStatus::CANCEL;
  }

  const size_t offset = this->insertEstimatedSuffix(ls, *candidates);
  if (const auto len = candidates->size(); len == 0) {
    linenoiseBeep(ls.ofd);
    return CompStatus::OK;
  } else if (len == 1) {
    return CompStatus::OK;
  } else {
    if (reader.fetch() <= 0) {
      return CompStatus::ERROR;
    }
    if (reader.get() == KeyBindings::TAB) {
      reader.clear();
    } else {
      return CompStatus::OK;
    }

    bool show = true;
    if (len >= 100) {
      char msg[256];
      snprintf(msg, 256, "\r\nDisplay all %zu possibilities? (y or n) ", len);
      ssize_t r = write(ls.ofd, msg, strlen(msg));
      UNUSED(r);

      while (true) {
        if (reader.fetch() <= 0) {
          return CompStatus::ERROR;
        }
        auto code = reader.take();
        if (code == "y") {
          break;
        } else if (code == "n") {
          r = write(ls.ofd, "\r\n", strlen("\r\n"));
          UNUSED(r);
          show = false;
          break;
        } else if (code == KeyBindings::CTRL_C) {
          return CompStatus::CANCEL;
        } else {
          linenoiseBeep(ls.ofd);
        }
      }
    }

    if (show) {
      updateColumns(ls);
      showAllCandidates(ls.ps, ls.ofd, ls.cols, *candidates);
    }
    this->refreshLine(ls);

    // rotate candidates
    size_t rotateIndex = 0;
    size_t prevCanLen = 0;
    while (true) {
      if (reader.fetch() <= 0) {
        return CompStatus::ERROR;
      }
      if (reader.get() == KeyBindings::TAB) {
        reader.clear();
      } else {
        return CompStatus::OK;
      }

      revertInsert(ls, prevCanLen);
      const char *can = candidates->getValues()[rotateIndex].asCStr();
      assert(offset <= ls.pos);
      size_t prefixLen = ls.pos - offset;
      prevCanLen = strlen(can) - prefixLen;
      if (linenoiseEditInsert(ls, can + prefixLen, prevCanLen)) {
        this->refreshLine(ls);
      } else {
        return CompStatus::CANCEL; // FIXME: report error ?
      }

      if (rotateIndex == len - 1) {
        rotateIndex = 0;
        continue;
      }
      rotateIndex++;
    }
  }
  return CompStatus::OK;
}

DSValue LineEditorObject::kickCallback(DSState &state, DSValue &&callback, CallArgs &&callArgs) {
  int errNum = errno;
  auto oldStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto oldIFS = state.getGlobal(BuiltinVarOffset::IFS);

  bool restoreTTY = this->rawMode;
  if (restoreTTY) {
    this->disableRawMode(this->inFd);
  }
  auto ret = VM::callFunction(state, std::move(callback), std::move(callArgs));
  if (restoreTTY) {
    this->enableRawMode(this->inFd);
  }

  // restore state
  state.setGlobal(BuiltinVarOffset::EXIT_STATUS, std::move(oldStatus));
  state.setGlobal(BuiltinVarOffset::IFS, std::move(oldIFS));
  errno = errNum;
  return ret;
}

ObjPtr<ArrayObject> LineEditorObject::kickCompletionCallback(DSState &state, StringRef line) {
  assert(this->completionCallback);

  const auto *modType = getCurRuntimeModule(state);
  if (!modType) {
    modType = cast<ModType>(state.typePool.getModTypeById(1).asOk());
  }
  auto mod = state.getGlobal(modType->getIndex());
  auto args = makeArgs(std::move(mod), DSValue::createStr(line));
  DSValue callback = this->completionCallback;
  auto ret = this->kickCallback(state, std::move(callback), std::move(args));
  if (state.hasError()) {
    return nullptr;
  }
  return toObjPtr<ArrayObject>(ret);
}

bool LineEditorObject::kickHistSyncCallback(DSState &state, struct linenoiseState &l) {
  if (!this->history) {
    return false;
  }
  if (this->histSyncCallback) {
    this->kickCallback(state, this->histSyncCallback,
                       makeArgs(DSValue::createStr(l.lineRef()), this->history));
    return !state.hasError();
  } else {
    return this->history->append(state, DSValue::createStr(l.lineRef()));
  }
}

bool LineEditorObject::kickCustomCallback(DSState &state, struct linenoiseState &l,
                                          CustomActionType type, unsigned int index) {
  StringRef line;
  auto optArg = DSValue::createInvalid();
  switch (type) {
  case CustomActionType::REPLACE_WHOLE:
  case CustomActionType::REPLACE_WHOLE_ACCEPT:
    line = l.lineRef();
    break;
  case CustomActionType::REPLACE_LINE: {
    auto [pos, len] = findLineInterval(l, true);
    line = l.lineRef();
    line = line.substr(pos, len);
    break;
  }
  case CustomActionType::INSERT:
    line = "";
    break;
  }

  auto ret = this->kickCallback(state, this->customCallbacks[index],
                                makeArgs(DSValue::createStr(line), std::move(optArg)));
  if (state.hasError()) {
    return false;
  }
  if (ret.isInvalid()) {
    return true;
  }
  assert(ret.hasStrRef());
  switch (type) {
  case CustomActionType::REPLACE_WHOLE:
  case CustomActionType::REPLACE_WHOLE_ACCEPT:
    l.len = l.pos = 0;
    break;
  case CustomActionType::REPLACE_LINE:
    linenoiseEditDeleteTo(l, true);
    break;
  case CustomActionType::INSERT:
    break;
  }
  const char *ptr = ret.asCStr();
  return linenoiseEditInsert(l, ptr, strlen(ptr));
}

bool LineEditorObject::addKeyBind(DSState &state, StringRef key, StringRef name) {
  auto s = this->keyBindings.addBinding(key, name);
  std::string message;
  switch (s) {
  case KeyBindings::AddStatus::OK:
    break;
  case KeyBindings::AddStatus::UNDEF:
    message = "undefined edit action: `";
    message += toPrintable(name);
    message += "'";
    break;
  case KeyBindings::AddStatus::FORBIT_BRACKET_START_CODE:
    message = "cannot change binding of bracket start code `";
    message += KeyBindings::toCaret(KeyBindings::BRACKET_START);
    message += "'";
    break;
  case KeyBindings::AddStatus::FORBTT_BRACKET_ACTION:
    message = "cannot bind to `";
    message += toString(EditActionType::BRACKET_PASTE);
    message += "'";
    break;
  case KeyBindings::AddStatus::INVALID_START_CHAR:
    message = "keycode must start with control character: `";
    message += toPrintable(key);
    message += "'";
    break;
  case KeyBindings::AddStatus::INVALID_ASCII:
    message = "keycode must be ascii characters: `";
    message += toPrintable(key);
    message += "'";
    break;
  case KeyBindings::AddStatus::LIMIT:
    message = "number of key bindings reaches limit (up to ";
    message += std::to_string(SYS_LIMIT_KEY_BINDING_MAX);
    message += ")";
    break;
  }
  if (!message.empty()) {
    raiseError(state, TYPE::InvalidOperationError, std::move(message));
    return false;
  }
  return true;
}

bool LineEditorObject::defineCustomAction(DSState &state, StringRef name, StringRef type,
                                          ObjPtr<DSObject> callback) {
  auto s = this->keyBindings.defineCustomAction(name, type);
  if (s) {
    assert(this->customCallbacks.size() == s.asOk());
    this->customCallbacks.push_back(std::move(callback));
    return true;
  }

  std::string message;
  switch (s.asErr()) {
  case KeyBindings::DefineError::INVALID_NAME:
    message += "invalid action name, must [a-zA-Z_-]: `";
    message += toPrintable(name);
    message += "'";
    break;
  case KeyBindings::DefineError::INVALID_TYPE:
    message += "unsupported custom action type: `";
    message += toPrintable(type);
    message += "'";
    break;
  case KeyBindings::DefineError::DEFINED:
    message += "already defined action: `";
    message += name;
    message += "'";
    break;
  case KeyBindings::DefineError::LIMIT:
    message += "number of custom actions reaches limit (up to ";
    message += std::to_string(SYS_LIMIT_CUSTOM_ACTION_MAX);
    message += ")";
    break;
  }
  raiseError(state, TYPE::InvalidOperationError, std::move(message));
  return false;
}

} // namespace ydsh