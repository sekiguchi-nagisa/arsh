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

#include "line_buffer.h"
#include "line_editor.h"
#include "vm.h"

// ++++++++++ copied from linenoise.c ++++++++++++++

#define UNUSED(x) (void)(x)
static const char *unsupported_term[] = {"dumb", "cons25", "emacs", nullptr};

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
  int ifd;                /* Terminal stdin file descriptor. */
  int ofd;                /* Terminal stdout file descriptor. */
  LineBuffer buf;         /* Edited line buffer. */
  StringRef prompt;       /* Prompt to display. */
  unsigned int oldColPos; /* Previous refresh cursor column position. */
  unsigned int oldRow;    /* Previous refresh cursor row position. */
  unsigned int rows;      /* Number of rows in terminal. */
  unsigned int cols;      /* Number of columns in terminal. */
  unsigned int maxRows;   /* Maximum num of rows used so far (multiline mode) */
  CharWidthProperties ps;
  bool rotating;
  unsigned int yankedSize;
};

enum KEY_ACTION {
  ESC = 27, /* Escape */
};

/* Debugging macro. */
#if 0
FILE *lndebug_fp = nullptr;
#define lndebug(...)                                                                               \
  do {                                                                                             \
    if (lndebug_fp == nullptr) {                                                                   \
      lndebug_fp = fopen("/dev/pts/2", "a");                                                       \
    }                                                                                              \
    fprintf(lndebug_fp,                                                                            \
            "\n[len=%d, pos=%d, oldcolpos=%d, oldrow=%d] rows: %d, maxRows: %d, oldmax: %d\n",     \
            (int)l.buf.getUsedSize(), (int)l.buf.getCursor(), (int)l.oldColPos, (int)l.oldRow,     \
            (int)rows, (int)l.maxRows, oldRows);                                                   \
    fprintf(lndebug_fp, ", " __VA_ARGS__);                                                         \
    fflush(lndebug_fp);                                                                            \
  } while (0)
#else
#define lndebug(fmt, ...)
#endif

/* ======================= Low level terminal handling ====================== */

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static bool isUnsupportedTerm(int fd) {
  auto tcpgid = tcgetpgrp(fd);
  auto pgid = getpgrp();
  if (tcpgid == -1 || pgid == -1 || tcpgid != pgid) {
    return true;
  }

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

struct WinSize {
  unsigned int rows{24};
  unsigned int cols{80};
};

static WinSize getWinSize(int fd) {
  WinSize size;
  struct winsize ws; // NOLINT
  if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
    if (ws.ws_row) {
      size.rows = ws.ws_row;
    }
    if (ws.ws_col) {
      size.cols = ws.ws_col;
    }
  }
  return size;
}

static void updateWinSize(struct linenoiseState &ls) {
  auto ret = getWinSize(ls.ifd);
  ls.rows = ret.rows;
  ls.cols = ret.cols;
}

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
     * if run under terminal multiplexer (screen/tmux), disable character width checking
     */
    return;
  }

  for (auto &e : getCharWidthPropertyList()) {
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

static bool linenoiseEditDeleteTo(LineBuffer &buf, KillRing &killRing) {
  std::string out;
  bool r = buf.deleteLineToCursor(false, &out);
  if (r) {
    killRing.add(std::move(out));
  }
  return r;
}

static bool linenoiseEditDeleteFrom(LineBuffer &buf, KillRing &killRing) {
  std::string out;
  bool r = buf.deleteLineFromCursor(&out);
  if (r) {
    killRing.add(std::move(out));
  }
  return r;
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. */
static bool linenoiseEditDeletePrevWord(LineBuffer &buf, KillRing &killRing) {
  std::string out;
  bool r = buf.deletePrevWord(&out);
  if (r) {
    killRing.add(std::move(out));
  }
  return r;
}

static bool linenoiseEditDeleteNextWord(LineBuffer &buf, KillRing &killRing) {
  std::string out;
  bool r = buf.deleteNextWord(&out);
  if (r) {
    killRing.add(std::move(out));
  }
  return r;
}

static bool linenoiseEditSwapChars(LineBuffer &buf) {
  if (buf.getCursor() == 0) { //  does not swap
    return false;
  } else if (buf.getCursor() == buf.getUsedSize()) {
    buf.moveCursorToLeftByChar();
  }
  std::string cutStr;
  return buf.deletePrevChar(&cutStr) && buf.moveCursorToRightByChar() && buf.insertToCursor(cutStr);
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static ssize_t linenoiseNoTTY(int inFd, char *buf, size_t bufLen) {
  assert(bufLen <= INT32_MAX && bufLen > 0);
  bufLen--; // reserve for null terminate
  size_t len = 0;
  while (true) {
    char data[64];
    ssize_t readSize = read(inFd, data, std::size(data));
    if (readSize == -1 && errno == EAGAIN) {
      continue;
    }
    if (readSize == 0) {
      break;
    }
    if (readSize < 0) {
      return -1;
    }
    auto size = static_cast<size_t>(readSize);
    if (len + size <= bufLen) {
      memcpy(buf + len, data, size);
      len += size;
    } else {
      errno = ENOMEM;
      return -1;
    }
  }
  buf[len] = '\0';
  return static_cast<ssize_t>(len);
}

// +++++++++++++++++++++++++++++++++++++++++++++++++++

namespace ydsh {

// ##############################
// ##     LineEditorObject     ##
// ##############################

LineEditorObject::LineEditorObject() : ObjectWithRtti(TYPE::LineEditor) {
  if (int ttyFd = open("/dev/tty", O_RDWR | O_CLOEXEC); ttyFd > -1) {
    this->inFd = ttyFd;
    this->outFd = this->inFd;
  } else { // fallback
    this->inFd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
    this->outFd = STDOUT_FILENO;
  }
}

LineEditorObject::~LineEditorObject() { close(this->inFd); }

static void enableBracketPasteMode(int fd) {
  const char *s = "\x1b[?2004h";
  if (write(fd, s, strlen(s)) == -1) {
  }
}

static void disableBracketPasteMode(int fd) {
  const char *s = "\x1b[?2004l";
  if (write(fd, s, strlen(s)) == -1) {
  }
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
  enableBracketPasteMode(fd);
  return 0;

fatal:
  errno = ENOTTY;
  return -1;
}

void LineEditorObject::disableRawMode(int fd) {
  disableBracketPasteMode(fd);
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
  checkProperty(l);
  return 0;
}

static std::pair<unsigned int, unsigned int> renderPrompt(const struct linenoiseState &l,
                                                          std::string &out) {
  LineRenderer renderer(l.ps, 0, out);
  renderer.setMaxCols(l.cols);
  renderer.renderWithANSI(l.prompt);
  auto promptRows = static_cast<unsigned int>(renderer.getTotalRows());
  auto promptCols = static_cast<unsigned int>(renderer.getTotalCols());
  return {promptRows, promptCols};
}

static std::pair<unsigned int, bool> renderLines(const struct linenoiseState &l, size_t promptCols,
                                                 ObserverPtr<const ANSIEscapeSeqMap> escapeSeqMap,
                                                 ObserverPtr<ArrayPager> pager, std::string &out) {
  size_t rows = 0;
  StringRef lineRef = l.buf.get();
  if (pager) {
    auto [pos, len] = l.buf.findCurLineInterval(true);
    lineRef = lineRef.substr(0, pos + len);
    if (!lineRef.endsWith("\n")) {
      rows++;
    }
  }

  bool continueLine = false;
  LineRenderer renderer(l.ps, promptCols, out, escapeSeqMap);
  renderer.setMaxCols(l.cols);
  if (escapeSeqMap) { // FIXME: cache previous rendered content
    continueLine = !renderer.renderScript(lineRef);
  } else {
    renderer.renderLines(lineRef);
  }
  rows += renderer.getTotalRows();
  if (pager) {
    if (!lineRef.endsWith("\n")) {
      out += "\r\n"; // force newline
    }
    pager->render(out);
    rows += pager->getRenderedRows();
  }
  return {static_cast<unsigned int>(rows), continueLine};
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
void LineEditorObject::refreshLine(struct linenoiseState &l, bool repaint,
                                   ObserverPtr<ArrayPager> pager) {
  updateWinSize(l);
  if (pager) {
    pager->updateWinSize({.rows = l.rows, .cols = l.cols});
  }
  if (repaint) {
    l.buf.syncNewlinePosList();
  }

  /* render and compute prompt row/column length */
  std::string lineBuf; // for rendered lines
  auto [promptRows, promptCols] = renderPrompt(l, lineBuf);

  /* render and compute line row/columns length */
  auto [rows, continueLine2] = renderLines(
      l, promptCols, this->highlight ? makeObserver(this->escapeSeqMap) : nullptr, pager, lineBuf);
  rows += promptRows + 1;
  this->continueLine = continueLine2;

  /* cursor relative row. */
  const int relativeRows = static_cast<int>(l.oldRow);
  const int oldRows = static_cast<int>(l.maxRows);
  std::string ab;

  lndebug("cols: %d, promptCols: %d, promptRows: %d, rows: %d", (int)l.cols, (int)promptCols,
          (int)promptRows, (int)rows);

  /* Update maxRows if needed. */
  if (rows > l.maxRows) {
    l.maxRows = rows;
  }

  /* First step: clear all the lines used before. To do so start by
   * going to the last row. */
  char seq[64];
  if (oldRows - relativeRows > 0) {
    lndebug("go down %d", oldRows - relativeRows);
    snprintf(seq, 64, "\x1b[%dB", oldRows - relativeRows);
    ab += seq;
  }

  /* Now for every row clear it, go up. */
  for (int j = 0; j < oldRows - 1; j++) {
    lndebug("clear+up");
    ab += "\r\x1b[0K\x1b[1A";
  }

  /* Clean the top line. */
  lndebug("clear");
  ab += "\r\x1b[0K";

  /* set escape sequence */
  lineBuf.insert(0, ab);
  ab = std::move(lineBuf);

  /* get cursor row/column length */
  size_t cursorCols = 0;
  size_t cursorRows = promptRows + 1;
  {
    auto ref = l.buf.getToCursor();
    LineRenderer renderer(l.ps, promptCols);
    renderer.setMaxCols(l.cols);
    renderer.renderLines(ref);
    cursorCols = renderer.getTotalCols();
    cursorRows += renderer.getTotalRows();
  }
  lndebug("cursor: cursorCols: %zu, cursorRows: %zu", cursorCols, cursorRows);

  /* Go up till we reach the expected position. */
  if (rows - cursorRows > 0) {
    lndebug("go-up %d", (int)rows - (int)cursorRows);
    snprintf(seq, 64, "\x1b[%dA", (int)rows - (int)cursorRows);
    ab += seq;
  }

  /* Set column position, zero-based. */
  lndebug("set col %d", 1 + (int)cursorCols);
  if (cursorCols) {
    snprintf(seq, 64, "\r\x1b[%dC", (int)cursorCols);
  } else {
    snprintf(seq, 64, "\r");
  }
  ab += seq;

  lndebug("\n");
  l.oldColPos = cursorCols;
  l.oldRow = cursorRows;

  if (write(l.ofd, ab.c_str(), ab.size()) == -1) {
  } /* Can't recover from write error. */
}

ssize_t LineEditorObject::accept(DSState &state, struct linenoiseState &l) {
  this->kickHistSyncCallback(state, l.buf);
  l.buf.clearNewlinePosList(); // force move cursor to end (force enter single line mode)
  if (l.buf.moveCursorToEndOfLine()) {
    this->refreshLine(l, false);
  }
  if (state.hasError()) {
    errno = EAGAIN;
    return -1;
  }
  return static_cast<ssize_t>(l.buf.getUsedSize());
}

static bool rotateHistory(HistRotator &histRotate, bool continueRotate, LineBuffer &buf,
                          HistRotator::Op op, bool multiline) {
  if (!histRotate) {
    return false;
  }
  multiline = multiline && !buf.isSingleLine();

  auto curBuf = buf.getCurLine(true);
  if (!histRotate.rotate(curBuf, op)) {
    return false;
  }
  if (continueRotate) {
    buf.undo();
  } else {
    if (multiline) {
      buf.deleteLineToCursor(true, nullptr);
    } else {
      buf.deleteAll();
    }
  }
  return buf.insertToCursor(curBuf);
}

static bool rotateHistoryOrUpDown(HistRotator &histRotate, struct linenoiseState &l,
                                  HistRotator::Op op, bool continueRotate) {
  if (l.buf.isSingleLine() || continueRotate) {
    l.rotating = rotateHistory(histRotate, continueRotate, l.buf, op, false);
    return l.rotating;
  } else {
    return l.buf.moveCursorUpDown(op == HistRotator::Op::PREV);
  }
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
ssize_t LineEditorObject::editLine(DSState &state, StringRef prompt, char *buf, size_t bufSize) {
  if (this->enableRawMode(this->inFd)) {
    return -1;
  }

  /* Populate the linenoise state that we pass to functions implementing
   * specific editing functionalities. */
  struct linenoiseState l = {
      .ifd = this->inFd,
      .ofd = this->outFd,
      .buf = LineBuffer(buf, bufSize),
      .prompt = prompt,
      .oldColPos = 0,
      .oldRow = 0,
      .rows = 24,
      .cols = 80,
      .maxRows = 0,
      .ps = {},
      .rotating = false,
      .yankedSize = 0,
  };

  l.ps.replaceInvalid = true;

  const ssize_t count = this->editInRawMode(state, l);
  const int errNum = errno;
  if (count == -1 && errNum != 0) {
    l.buf.clearNewlinePosList(); // force move cursor to end (force enter single line mode)
    if (l.buf.moveCursorToEndOfLine()) {
      this->refreshLine(l, false);
    }
  }
  this->disableRawMode(this->inFd);
  ssize_t r = write(this->outFd, "\n", 1);
  UNUSED(r);
  errno = errNum;
  return count;
}

ssize_t LineEditorObject::editInRawMode(DSState &state, struct linenoiseState &l) {
  /* The latest history entry is always our current buffer, that
   * initially is just an empty string. */
  if (unlikely(this->history && !this->history->checkIteratorInvalidation(state))) {
    errno = EAGAIN;
    return -1;
  }
  HistRotator histRotate(this->history);

  preparePrompt(l);
  this->refreshLine(l);

  KeyCodeReader reader(l.ifd);
  while (true) {
    if (ssize_t r = reader.fetch(); r <= 0) {
      if (r == -1) {
        return -1;
      }
      return static_cast<ssize_t>(l.buf.getUsedSize());
    }

  NO_FETCH:
    const bool prevRotating = l.rotating;
    l.rotating = false;
    const unsigned int prevYankedSize = l.yankedSize;
    l.yankedSize = 0;

    if (!reader.hasControlChar()) {
      auto &buf = reader.get();
      bool merge = buf != " ";
      if (l.buf.insertToCursor(buf, merge)) {
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
        if (l.buf.insertToCursor({"\n", 1})) {
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
          return static_cast<int>(l.buf.getUsedSize());
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
      if (l.buf.deletePrevChar(nullptr, true)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::DELETE_CHAR:
      if (l.buf.deleteNextChar(nullptr, true)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::DELETE_OR_EXIT: /* remove char at right of cursor, or if the line is empty,
                                        act as end-of-file. */
      if (l.buf.getUsedSize() > 0) {
        if (l.buf.deleteNextChar(nullptr, true)) {
          this->refreshLine(l);
        }
      } else {
        errno = 0;
        return -1;
      }
      break;
    case EditActionType::TRANSPOSE_CHAR: /* swaps current character with previous */
      if (linenoiseEditSwapChars(l.buf)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::BACKWARD_CHAR:
      if (l.buf.moveCursorToLeftByChar()) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::FORWARD_CHAR:
      if (l.buf.moveCursorToRightByChar()) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::PREV_HISTORY:
    case EditActionType::NEXT_HISTORY: {
      auto op = action->type == EditActionType::PREV_HISTORY ? HistRotator::Op::PREV
                                                             : HistRotator::Op::NEXT;
      if (rotateHistory(histRotate, prevRotating, l.buf, op, true)) {
        l.rotating = true;
        this->refreshLine(l);
      }
      break;
    }
    case EditActionType::UP_OR_HISTORY:
    case EditActionType::DOWN_OR_HISTORY: {
      auto op = action->type == EditActionType::UP_OR_HISTORY ? HistRotator::Op::PREV
                                                              : HistRotator::Op::NEXT;
      if (rotateHistoryOrUpDown(histRotate, l, op, prevRotating)) {
        this->refreshLine(l);
      }
      break;
    }
    case EditActionType::BACKWORD_KILL_LINE: /* delete the whole line or delete to current */
      if (linenoiseEditDeleteTo(l.buf, this->killRing)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::KILL_LINE: /* delete from current to end of line */
      if (linenoiseEditDeleteFrom(l.buf, this->killRing)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::BEGINNING_OF_LINE: /* go to the start of the line */
      if (l.buf.moveCursorToStartOfLine()) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::END_OF_LINE: /* go to the end of the line */
      if (l.buf.moveCursorToEndOfLine()) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::BEGINNING_OF_BUF: /* go to the start of the buffer */
      if (l.buf.getCursor() != 0) {
        l.buf.setCursor(0);
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::END_OF_BUF: /* go to the end of the buffer */
      if (l.buf.getCursor() != l.buf.getUsedSize()) {
        l.buf.setCursor(l.buf.getUsedSize());
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::CLEAR_SCREEN:
      linenoiseClearScreen(l.ofd);
      this->refreshLine(l);
      break;
    case EditActionType::BACKWARD_KILL_WORD:
      if (linenoiseEditDeletePrevWord(l.buf, this->killRing)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::KILL_WORD:
      if (linenoiseEditDeleteNextWord(l.buf, this->killRing)) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::BACKWARD_WORD:
      if (l.buf.moveCursorToLeftByWord()) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::FORWARD_WORD:
      if (l.buf.moveCursorToRightByWord()) {
        this->refreshLine(l, false);
      }
      break;
    case EditActionType::NEWLINE:
      if (l.buf.insertToCursor({"\n", 1})) {
        this->refreshLine(l);
      } else {
        return -1;
      }
      break;
    case EditActionType::YANK:
      if (this->killRing) {
        this->killRing.reset();
        StringRef line = this->killRing.getCurrent();
        if (!line.empty()) {
          l.yankedSize = line.size();
          if (l.buf.insertToCursor(line)) {
            this->refreshLine(l);
          } else {
            return -1;
          }
        }
      }
      break;
    case EditActionType::YANK_POP:
      if (prevYankedSize > 0) {
        assert(this->killRing);
        l.buf.undo();
        this->killRing.rotate();
        StringRef line = this->killRing.getCurrent();
        if (!line.empty()) {
          l.yankedSize = line.size();
          if (l.buf.insertToCursor(line)) {
            this->refreshLine(l);
          } else {
            return -1;
          }
        }
      }
      break;
    case EditActionType::UNDO:
      if (l.buf.undo()) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::REDO:
      if (l.buf.redo()) {
        this->refreshLine(l);
      }
      break;
    case EditActionType::INSERT_KEYCODE:
      if (reader.fetch() > 0) {
        auto &buf = reader.get();
        bool merge = buf != " " && buf != "\n";
        if (l.buf.insertToCursor(buf, merge)) {
          this->refreshLine(l);
        } else {
          return -1;
        }
      }
      break;
    case EditActionType::BRACKET_PASTE: {
      l.buf.fixLastChange();
      bool r = reader.intoBracketedPasteMode(
          [&l](StringRef ref) { return l.buf.insertToCursor(ref, true); });
      int old = errno;
      l.buf.fixLastChange();
      this->refreshLine(l); // always refresh line even if error
      if (!r) {
        errno = old;
        return -1;
      }
      break;
    }
    case EditActionType::CUSTOM: {
      bool r = this->kickCustomCallback(state, l.buf, action->customActionType,
                                        action->customActionIndex);
      this->refreshLine(l); // always refresh line even if error
      if (r && action->customActionType == CustomActionType::REPLACE_WHOLE_ACCEPT) {
        histRotate.revertAll();
        return this->accept(state, l);
      } else if (state.hasError()) {
        errno = EAGAIN;
        return -1;
      }
      break;
    }
    }
  }
  return static_cast<ssize_t>(l.buf.getUsedSize());
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
ssize_t LineEditorObject::readline(DSState &state, StringRef prompt, char *buf, size_t bufLen) {
  if (bufLen == 0 || bufLen > INT32_MAX) {
    errno = EINVAL;
    return -1;
  }

  errno = 0;
  if (!isatty(this->inFd)) {
    /* Not a tty: read from file / pipe. In this mode we don't want any
     * limit to the line size, so we call a function to handle that. */
    return linenoiseNoTTY(this->inFd, buf, bufLen);
  }

  this->lock = true;
  this->continueLine = false;
  auto cleanup = finally([&] { this->lock = false; });

  // prepare prompt
  DSValue promptVal;
  if (this->promptCallback) {
    auto args = makeArgs(DSValue::createStr(prompt));
    DSValue callback = this->promptCallback;
    promptVal = this->kickCallback(state, std::move(callback), std::move(args));
    if (state.hasError()) {
      errno = EAGAIN;
      return -1;
    }
  }
  if (promptVal.hasStrRef()) {
    prompt = promptVal.asStrRef();
  }
  if (isUnsupportedTerm(this->inFd)) {
    ssize_t r = write(this->outFd, prompt.data(), prompt.size());
    UNUSED(r);
    fsync(this->outFd);
    bufLen--; // preserve for null terminated
    ssize_t rlen = read(this->inFd, buf, bufLen);
    if (rlen < 0) {
      return -1;
    }
    auto len = static_cast<size_t>(rlen);
    buf[len] = '\0';
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      len--;
      buf[len] = '\0';
    }
    return static_cast<ssize_t>(len);
  } else {
    return this->editLine(state, prompt, buf, bufLen);
  }
}

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

/**
 *
 * @param buf
 * @param candidates
 * @param inserting
 * @return
 * token start cursor
 */
static size_t resolveEstimatedSuffix(const LineBuffer &buf, const ArrayObject &candidates,
                                     StringRef &inserting) {
  const size_t cursor = buf.getCursor();
  const auto prefix = candidates.getCommonPrefixStr();
  inserting = "";

  logprintf("#prefix: %s\n", prefix.toString().c_str());
  logprintf("cursor: %ld\n", cursor);

  // compute suffix
  bool matched = false;
  size_t offset = cursor - std::min(cursor, prefix.size());
  for (; offset < cursor; offset++) {
    auto suffix = buf.get().substr(offset, cursor - offset);
    logprintf("curSuffix: %s\n", suffix.toString().c_str());
    if (prefix.startsWith(suffix)) {
      matched = true;
      break;
    }
  }

  logprintf("offset: %ld\n", offset);
  if (matched) {
    size_t insertingSize = prefix.size() - (cursor - offset);
    inserting = {prefix.data() + (prefix.size() - insertingSize), insertingSize};
    logprintf("inserting: %s\n", inserting.toString().c_str());
  } else if (candidates.size() == 1) {
    inserting = prefix; // if candidate does not match previous token, use common candidate prefix
  }
  return matched ? offset : buf.getCursor();
}

static LineEditorObject::CompStatus waitPagerAction(ArrayPager &pager, const KeyBindings &bindings,
                                                    KeyCodeReader &reader) {
  // read key code and update pager state
  if (reader.fetch() <= 0) {
    return LineEditorObject::CompStatus::ERROR;
  }
  if (!reader.hasControlChar()) {
    return LineEditorObject::CompStatus::OK;
  }
  const auto *action = bindings.findPagerAction(reader.get());
  if (!action) {
    return LineEditorObject::CompStatus::OK;
  }
  reader.clear();
  switch (*action) {
  case PagerAction::SELECT:
    return LineEditorObject::CompStatus::OK;
  case PagerAction::CANCEL:
    return LineEditorObject::CompStatus::CANCEL;
  case PagerAction::PREV:
    pager.moveCursorToForward();
    break;
  case PagerAction::NEXT:
    pager.moveCursorToNext();
    break;
  case PagerAction::LEFT:
    pager.moveCursorToLeft();
    break;
  case PagerAction::RIGHT:
    pager.moveCursorToRight();
    break;
  }
  return LineEditorObject::CompStatus::CONTINUE;
}

LineEditorObject::CompStatus
LineEditorObject::completeLine(DSState &state, struct linenoiseState &ls, KeyCodeReader &reader) {
  reader.clear();

  auto candidates = this->kickCompletionCallback(state, ls.buf.getToCursor());
  if (!candidates || candidates->size() <= 1) {
    this->refreshLine(ls);
  }
  if (!candidates) {
    return CompStatus::CANCEL;
  }

  StringRef inserting;
  const size_t offset = resolveEstimatedSuffix(ls.buf, *candidates, inserting);
  if (candidates->size() > 0 && !inserting.empty()) {
    if (ls.buf.insertToCursor(inserting)) {
      this->refreshLine(ls); // FIXME: report error
    }
  }
  if (const auto len = candidates->size(); len == 0) {
    linenoiseBeep(ls.ofd);
    return CompStatus::OK;
  } else if (len == 1) {
    return CompStatus::OK;
  } else {
    auto status = CompStatus::CONTINUE;
    auto pager = ArrayPager::create(*candidates, ls.ps, {.rows = ls.rows, .cols = ls.cols});

    /**
     * first, only show pager and wait next completion action.
     * if next action is not completion action, break paging
     */
    pager.setShowCursor(false);
    this->refreshLine(ls, true, makeObserver(pager));
    if (reader.fetch() <= 0) {
      status = CompStatus::ERROR;
      goto END;
    }
    if (!reader.hasControlChar()) {
      status = CompStatus::OK;
      goto END;
    }
    if (auto *action = this->keyBindings.findAction(reader.get());
        !action || action->type != EditActionType::COMPLETE) {
      status = CompStatus::OK;
      goto END;
    }

    /**
     * paging completion candidates
     */
    pager.setShowCursor(true);
    for (size_t prevCanLen = 0; status == CompStatus::CONTINUE;) {
      // render pager
      if (prevCanLen) {
        ls.buf.undo();
      }
      const auto can = candidates->getValues()[pager.getIndex()].asStrRef();
      assert(offset <= ls.buf.getCursor());
      size_t prefixLen = ls.buf.getCursor() - offset;
      prevCanLen = can.size() - prefixLen;
      if (ls.buf.insertToCursor({can.data() + prefixLen, prevCanLen})) {
        this->refreshLine(ls, true, makeObserver(pager));
      } else {
        status = CompStatus::ERROR;
        break;
      }
      status = waitPagerAction(pager, this->keyBindings, reader);
    }

  END:
    this->refreshLine(ls); // clear pager
    return status;
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
    modType = state.typePool.getModTypeById(ROOT_MOD_ID);
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

bool LineEditorObject::kickHistSyncCallback(DSState &state, const LineBuffer &buf) {
  if (!this->history) {
    return true;
  }
  if (this->histSyncCallback) {
    this->kickCallback(state, this->histSyncCallback,
                       makeArgs(DSValue::createStr(buf.get()), this->history));
    return !state.hasError();
  } else {
    return this->history->append(state, DSValue::createStr(buf.get()));
  }
}

bool LineEditorObject::kickCustomCallback(DSState &state, LineBuffer &buf, CustomActionType type,
                                          unsigned int index) {
  StringRef line;
  auto optArg = DSValue::createInvalid();
  switch (type) {
  case CustomActionType::REPLACE_WHOLE:
  case CustomActionType::REPLACE_WHOLE_ACCEPT:
    line = buf.get();
    break;
  case CustomActionType::REPLACE_LINE:
  case CustomActionType::HIST_SELCT: {
    if (type == CustomActionType::HIST_SELCT) {
      if (!this->history) {
        return false;
      }
      optArg = this->history;
    }
    line = buf.getCurLine(true);
    break;
  }
  case CustomActionType::INSERT:
    line = "";
    break;
  case CustomActionType::KILL_RING_SELECT:
    if (!this->killRing) {
      return true; // do nothing
    }
    line = "";
    optArg = this->killRing.toObj(state.typePool);
    break;
  }

  auto ret = this->kickCallback(state, this->customCallbacks[index],
                                makeArgs(DSValue::createStr(line), std::move(optArg)));
  if (state.hasError()) {
    return false;
  }
  if (ret.isInvalid()) {
    return false;
  }
  assert(ret.hasStrRef());
  switch (type) {
  case CustomActionType::REPLACE_WHOLE:
  case CustomActionType::REPLACE_WHOLE_ACCEPT:
    buf.deleteAll();
    break;
  case CustomActionType::REPLACE_LINE:
  case CustomActionType::HIST_SELCT:
    buf.deleteLineToCursor(true, nullptr);
    break;
  case CustomActionType::INSERT:
  case CustomActionType::KILL_RING_SELECT:
    break;
  }
  auto ref = ret.asStrRef();
  return buf.insertToCursor(ref);
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
  case KeyBindings::AddStatus::FORBID_BRACKET_START_CODE:
    message = "cannot change binding of bracket start code `";
    message += KeyBindings::toCaret(KeyBindings::BRACKET_START);
    message += "'";
    break;
  case KeyBindings::AddStatus::FORBID_BRACKET_ACTION:
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
    raiseError(state, TYPE::ArgumentError, std::move(message));
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
  raiseError(state, TYPE::ArgumentError, std::move(message));
  return false;
}

} // namespace ydsh