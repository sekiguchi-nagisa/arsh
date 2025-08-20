/*
 * Copyright (C) 2022-2024 Nagisa Sekiguchi <s dot nagisa dot xyz at gmail dot com>
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
#include <unistd.h>

#include "line_buffer.h"
#include "line_editor.h"
#include "logger.h"
#include "misc/pty.hpp"
#include "pager.h"
#include "token_edit.h"
#include "vm.h"

// ++++++++++ copied from linenoise.c ++++++++++++++

#define UNUSED(x) (void)(x)
static const char *unsupported_term[] = {"dumb", "cons25", "emacs", nullptr};

/* ======================= Low level terminal handling ====================== */

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static bool isUnsupportedTerm(const int fd) {
  const auto tcpgid = tcgetpgrp(fd);
  const auto pgid = getpgrp();
  if (tcpgid == -1 || pgid == -1 || tcpgid != pgid) {
    return true;
  }

  const char *term = getenv("TERM");
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
static int getCursorPosition(int ifd, int ofd, bool queryCursor) {
  char buf[32];
  int cols, rows;

  /* Report cursor location */
  if (queryCursor) {
    if (constexpr char data[] = "\x1b[6n"; write(ofd, data, std::size(data) - 1) != 4) {
      return -1;
    }
  }

  /* Read the response: ESC [ rows ; cols R */
  unsigned int i = 0;
  for (; i < sizeof(buf) - 1; i++) {
    if (readRetryWithTimeout(ifd, buf + i, 1, 2000) != 1) {
      break;
    }
    if (buf[i] == 'R') {
      break;
    }
  }
  buf[i] = '\0';
  LOG(TRACE_EDIT, "i=%d, buf:[%02x %02x %02x %02x %02x %02x %02x]", i, buf[0], buf[1], buf[2],
      buf[3], buf[4], buf[5], buf[6]);

  /* Parse it. */
  if (constexpr int ESC = 27; buf[0] != ESC || buf[1] != '[') {
    return -1;
  }
  if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) {
    return -1;
  }
  return cols;
}

/* Clear the screen. Used to handle ctrl+l */
static void linenoiseClearScreen(int fd) {
  if (constexpr char data[] = "\x1b[H\x1b[2J"; write(fd, data, std::size(data) - 1) <= 0) {
    /* nothing to do, just to avoid warning. */
  }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(int fd) {
  constexpr char data[] = "\x07";
  ssize_t r = write(fd, data, std::size(data) - 1);
  UNUSED(r);
  fsync(fd);
}

/**
 * workaround for screen/tmux
 * @return
 */
static bool underMultiplexer() {
  if (StringRef(getenv("TERM")).contains("screen")) {
    return true;
  }
  if (getenv("TMUX")) {
    return true;
  }
  return false;
}

/**
 * must call before initial line refresh
 * @param ps
 * @param inFd
 * @param outFd
 */
static void checkProperty(CharWidthProperties &ps, int inFd, int outFd) {
  if (underMultiplexer()) {
    /**
     * if run under terminal multiplexer (screen/tmux), disable character width checking
     */
    return;
  }

  for (auto &e : getCharWidthPropertyList()) {
    char buf[32];
    /**
     * hide cursor and clear line immediately (due to suppress cursor flicker)
     */
    const int s = snprintf(buf, std::size(buf), "\x1b[?25l<%s>\x1b[1K\x1b[6n\r", e.second);
    tcflush(inFd, TCIFLUSH); // force clear inbound data
    if (s < 0 || write(outFd, buf, s) == -1) {
      break;
    }
    const int pos = getCursorPosition(inFd, outFd, false);
    const int len = pos - 3;
    LOG(TRACE_EDIT, "char:<%s>, pos:%d, len:%d", e.second, pos, len);
    if (len <= 0) {
      continue; // skip unresolved property
    }
    ps.setProperty(e.first, len);
  }
}

/* =========================== Line editing ================================= */

static bool linenoiseEditSwapChars(LineBuffer &buf) {
  if (buf.getCursor() == 0) { //  does not swap
    return false;
  }
  if (buf.getCursor() == buf.getUsedSize()) {
    buf.moveCursorToLeftByChar();
  }

  bool s = false;
  buf.intoAtomicEdit([&s](LineBuffer &b) {
    std::string cutStr;
    s = b.deletePrevChar(&cutStr) && b.moveCursorToRightByChar() && b.insertToCursor(cutStr);
  });
  return s;
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
    const ssize_t readSize = read(inFd, data, std::size(data));
    if (readSize == -1 && errno == EAGAIN) {
      continue;
    }
    if (readSize == 0) {
      break;
    }
    if (readSize < 0) {
      return -1;
    }
    if (const auto size = static_cast<size_t>(readSize); len + size <= bufLen) {
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

namespace arsh {

// ##############################
// ##     LineEditorObject     ##
// ##############################

LineEditorObject::LineEditorObject(ARState &state) : ObjectWithRtti(TYPE::LineEditor) {
  if (const int ttyFd = open("/dev/tty", O_RDWR | O_CLOEXEC); ttyFd > -1) {
    this->inFd = ttyFd;
    remapFDCloseOnExec(this->inFd);
    this->outFd = this->inFd;
    syncWinSize(state, this->inFd, nullptr);
  } else { // fallback
    this->inFd = dupFDCloseOnExec(STDIN_FILENO);
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

static void enableKittyKeyboardProtocol(int fd) {
  /**
   * enable the following progressive enhancement
   * (see https://sw.kovidgoyal.net/kitty/keyboard-protocol/#progressive-enhancement)
   *
   * 0b1    Disambiguate escape codes
   * 0b100  Report alternate keys
   */
  const char *s = "\x1b[=5u"; // 0b101
  if (write(fd, s, strlen(s)) == -1) {
  }
}

static void disableKittyKeyboardProtocol(int fd) {
  const char *s = "\x1b[=0u"; // reset all enhancement flags
  if (write(fd, s, strlen(s)) == -1) {
  }
}

static void enableModifyOtherKeys(int fd) {
  const char *s = "\x1b[>4;1m";
  if (write(fd, s, strlen(s)) == -1) {
  }
}

static void disableModifyOtherKeys(int fd) {
  const char *s = "\x1b[>4;0m"; // reset all enhancement flags
  if (write(fd, s, strlen(s)) == -1) {
  }
}

/* Raw mode: 1960 magic shit. */
int LineEditorObject::enableRawMode(int fd) {
  termios raw{}; // NOLINT

  if (!isatty(fd)) {
    goto fatal;
  }
  if (tcgetattr(fd, &this->orgTermios) == -1) {
    goto fatal;
  }

  xcfmakesane(raw); /* modify the sane mode */
  /* input modes: no break, no CR to NL, no parity check, no strip char
   */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXANY | IMAXBEL);
  raw.c_iflag |= IUTF8 | IXOFF;
  if (this->hasFeature(LineEditorFeature::FLOW_CONTROL)) {
    raw.c_iflag |= IXON;
  } else {
    raw.c_iflag &= ~IXON;
  }
  /* output modes - disable post processing */
  //    raw.c_oflag &= ~(OPOST);
  raw.c_oflag &= ~TAB3;
  /* control modes - set 8 bit chars */
  raw.c_cflag |= (CS8);
  raw.c_cflag &= ~HUPCL;
  /* local modes - choing off, canonical off, no extended functions,
   * no signal chars (^Z,^C) */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  /* control chars - set return condition: min number of bytes and timer.
   * We want read to return every single byte, without timeout. */
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

  /* set speed */
#ifndef EXTB
#define EXTB B38400
#endif

  cfsetispeed(&raw, EXTB);
  cfsetospeed(&raw, EXTB);

  /* put terminal in raw mode after flushing */
  if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) {
    goto fatal;
  }
  this->rawMode = true;
  if (this->hasFeature(LineEditorFeature::BRACKETED_PASTE)) {
    enableBracketPasteMode(fd);
  }
  if (this->hasFeature(LineEditorFeature::KITTY_KEYBOARD_PROTOCOL)) {
    enableKittyKeyboardProtocol(fd);
  }
  if (this->hasFeature(LineEditorFeature::XTERM_MODIFY_OTHER_KEYS)) {
    enableModifyOtherKeys(fd);
  }
  return 0;

fatal:
  errno = ENOTTY;
  return -1;
}

void LineEditorObject::disableRawMode(int fd) {
  if (this->rawMode) {
    if (this->hasFeature(LineEditorFeature::BRACKETED_PASTE)) {
      disableBracketPasteMode(fd);
    }
    if (this->hasFeature(LineEditorFeature::KITTY_KEYBOARD_PROTOCOL)) {
      disableKittyKeyboardProtocol(fd);
    }
    if (this->hasFeature(LineEditorFeature::XTERM_MODIFY_OTHER_KEYS)) {
      disableModifyOtherKeys(fd);
    }
    /* Don't even check the return value as it's too late. */
    if (tcsetattr(fd, TCSAFLUSH, &this->orgTermios) != -1) {
      this->rawMode = false;
    }
  }
}

/**
 * if the current cursor is not head of line. write % symbol like zsh
 * @param inFd
 * @param outFd
 */
static int preparePrompt(int inFd, int outFd) {
  if (getCursorPosition(inFd, outFd, true) > 1) {
    const char *s = "\x1b[7m%\x1b[0m\r\n";
    if (write(outFd, s, strlen(s)) == -1) {
      return -1;
    }
  }
  return 0;
}

/* Multi-line low-level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
void LineEditorObject::refreshLine(ARState &state, RenderingContext &ctx, bool repaint,
                                   ObserverPtr<ArrayPager> pager) {
  WinSize winSize;
  syncWinSize(state, this->inFd, &winSize);

  if (pager) {
    pager->updateWinSize({.rows = winSize.rows, .cols = winSize.cols});
  }
  if (repaint) {
    ctx.buf.syncNewlinePosList();
  }

  auto ret = doRendering(ctx, pager,
                         this->hasFeature(LineEditorFeature::LANG_EXTENSION)
                             ? makeObserver(this->escapeSeqMap)
                             : nullptr,
                         winSize.cols);
  this->continueLine = ret.continueLine;
  const unsigned int actualCursorRows = ret.cursorRows;

  LOG(TRACE_EDIT, "[len=%u, pos=%u, oldCursorRows=%u, oldRenderedCols=%u]", ctx.buf.getUsedSize(),
      ctx.buf.getCursor(), ctx.oldCursorRows, ctx.oldRenderedCols);
  LOG(TRACE_EDIT, "(rows,cols)=(%u, %u)", winSize.rows, winSize.cols);
  LOG(TRACE_EDIT, "renderedRows: %zu, cursor(rows,cols)=(%zu,%zu)", ret.renderedRows,
      ret.cursorRows, ret.cursorCols);

  /*
   * hide cursor during rendering due to suppress potential cursor flicker
   */
  std::string ab = "\x1b[?25l"; // hide cursor (from VT220 extension)

  /* move cursor original position and clear screen */
  char seq[64];
  if (ctx.oldRenderedCols > winSize.cols) { // clear screen due to screen corruption
    ab += "\x1b[H\x1b[2J";
  } else {
    if (ctx.oldCursorRows > 1) { // set cursor original row position
      const auto diff = ctx.oldCursorRows - 1;
      LOG(TRACE_EDIT, "go up cursor: %u", diff);
      snprintf(seq, std::size(seq), "\x1b[%uA", diff);
      ab += seq;
    }
    /* Clean the top and bellow lines. */
    LOG(TRACE_EDIT, "clear");
    ab += "\r\x1b[0K\x1b[0J";
  }

  /* adjust too long rendered lines */
  LOG(TRACE_EDIT, "scrolling: %s", ctx.scrolling ? "true" : "false");
  ctx.scrolling = fitToWinSize(ctx, static_cast<bool>(pager), winSize.rows, ret);
  LOG(TRACE_EDIT, "adjust renderedRows: %zu. cursorRows: %zu", ret.renderedRows, ret.cursorRows);

  /* set escape sequence */
  ret.renderedLines.insert(0, ab);
  ab = std::move(ret.renderedLines);

  /* Go up till we reach the expected position. */
  if (const auto dist = static_cast<unsigned int>(ret.renderedRows - ret.cursorRows); dist > 0) {
    LOG(TRACE_EDIT, "go-up %u", dist);
    snprintf(seq, std::size(seq), "\x1b[%dA", dist);
    ab += seq;
  }

  /* Set column position, zero-based. */
  LOG(TRACE_EDIT, "set col %u", 1 + static_cast<unsigned int>(ret.cursorCols));
  if (ret.cursorCols) {
    snprintf(seq, std::size(seq), "\r\x1b[%uC", static_cast<unsigned int>(ret.cursorCols));
  } else {
    snprintf(seq, std::size(seq), "\r");
  }
  ab += seq;
  ab += "\x1b[?25h"; // show cursor (from VT220 extension)

  ctx.oldCursorRows = ret.cursorRows;
  ctx.oldActualCursorRows = actualCursorRows;
  ctx.oldRenderedCols = ret.renderedCols;

  if (write(this->outFd, ab.c_str(), ab.size()) == -1) {
  } /* Can't recover from write error. */
}

ssize_t LineEditorObject::accept(ARState &state, RenderingContext &ctx) {
  if (!this->kickHistSyncCallback(state, ctx.buf)) {
    errno = EAGAIN;
    return -1;
  }
  if (ctx.buf.moveCursorToEndOfBuf()) {
    this->refreshLine(state, ctx, false);
  }
  return static_cast<ssize_t>(ctx.buf.getUsedSize());
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

static bool rotateHistoryOrUpDown(HistRotator &histRotate, LineBuffer &buf, bool &rotating,
                                  HistRotator::Op op, bool continueRotate) {
  if (buf.isSingleLine() || continueRotate) {
    rotating = rotateHistory(histRotate, continueRotate, buf, op, false);
    return rotating;
  } else {
    return buf.moveCursorUpDown(op == HistRotator::Op::PREV);
  }
}

#define OSC133_(O) "\x1b]133;" O "\x1b\\"

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
ssize_t LineEditorObject::editLine(ARState &state, RenderingContext &ctx) {
  if (this->enableRawMode(this->inFd)) {
    return -1;
  }

  const ssize_t count = this->editInRawMode(state, ctx);
  const int errNum = errno;
  bool putNewline = true;
  if (count == -1 && errNum != 0) {
    if (ctx.scrolling) {
      linenoiseClearScreen(this->inFd);
      putNewline = false;
    } else if (ctx.buf.moveCursorToEndOfBuf()) {
      this->refreshLine(state, ctx, false);
    }
  }
  this->disableRawMode(this->inFd);
  if (putNewline) {
    dprintf(this->outFd, "\n%s",
            this->hasFeature(LineEditorFeature::SEMANTIC_PROMPT) ? OSC133_("C") : "");
  }
  errno = errNum;
  return count;
}

static AtomicSigSet toSigSet(const SignalVector &sigVector) {
  AtomicSigSet sigSet;
  for (auto &e : sigVector.getData()) {
    sigSet.add(e.first);
  }
  sigSet.add(SIGWINCH);
  return sigSet;
}

ssize_t LineEditorObject::editInRawMode(ARState &state, RenderingContext &ctx) {
  /* The latest history entry is always our current buffer, that
   * initially is just an empty string. */
  if (unlikely(this->history && !this->history->checkIteratorInvalidation(state))) {
    errno = EAGAIN;
    return -1;
  }
  HistRotator histRotate(this->history);

  preparePrompt(this->inFd, this->outFd);
  if (this->hasFeature(LineEditorFeature::SEMANTIC_PROMPT)) {
    // emit OSC133 before property check due to workaround for iTerm2
    dprintf(this->outFd, "\x1b]133;D;%d\x1b\\" OSC133_("A"), state.getMaskedExitStatus());
  }
  checkProperty(ctx.ps, this->inFd, this->outFd);
  if (this->eaw != 0) { // force set east asin width
    ctx.ps.eaw = this->eaw == 1 ? AmbiguousCharWidth::HALF : AmbiguousCharWidth::FULL;
  }
  state.setGlobal(BuiltinVarOffset::EAW,
                  Value::createInt(ctx.ps.eaw == AmbiguousCharWidth::HALF ? 1 : 2));
  this->refreshLine(state, ctx);

  bool rotating = false;
  unsigned int yankedSize = 0;
  KeyCodeReader reader(this->inFd);
  while (true) {
    if (ssize_t r = reader.fetch(toSigSet(state.sigVector)); r <= 0) {
      if (r == -1) {
        if (errno == EINTR) {
          if (this->handleSignals(state)) {
            this->refreshLine(state, ctx);
            continue;
          }
          if (state.hasError()) {
            errno = EAGAIN;
            return -1;
          }
        }
        return -1;
      }
      return static_cast<ssize_t>(ctx.buf.getUsedSize());
    }

  NO_FETCH:
    const bool prevRotating = rotating;
    rotating = false;
    const unsigned int prevYankedSize = yankedSize;
    yankedSize = 0;

    if (!reader.hasControlChar()) { // valid or invalid utf8 sequence
      auto &buf = reader.get();
      if (const bool merge = buf != " "; ctx.buf.insertToCursor(buf, merge)) {
        this->refreshLine(state, ctx);
        continue;
      }
      return -1;
    }
    if (const auto codePoint = reader.getEscapedPlainCodePoint(); codePoint > -1) {
      char buf[6];
      unsigned int r = UnicodeUtil::codePointToUtf8(codePoint, buf);
      if (ctx.buf.insertToCursor({buf, r}, codePoint != ' ')) {
        this->refreshLine(state, ctx);
        continue;
      }
      return -1;
    }
    if (reader.hasBracketedPasteStart()) {
      ctx.buf.commitLastChange();
      const auto oldTimeout = reader.getTimeout();
      reader.setTimeout(KeyCodeReader::DEFAULT_READ_TIMEOUT_MSEC * 2);
      bool r = reader.intoBracketedPasteMode(
          [&ctx](StringRef ref) { return ctx.buf.insertToCursor(ref, true); });
      reader.setTimeout(oldTimeout);
      const int old = errno;
      ctx.buf.commitLastChange();
      this->refreshLine(state, ctx); // always refresh the line even if error
      if (!r) {
        errno = old;
        return -1;
      }
      continue;
    }

    // dispatch edit action
    const auto *action = this->keyBindings.findAction(reader.getEvent());
    if (!action) {
      continue; // skip unbound key action
    }

    switch (action->type) {
    case EditActionType::ACCEPT:
      if (this->continueLine) {
        if (ctx.buf.insertToCursor({"\n", 1})) {
          this->refreshLine(state, ctx);
        } else {
          return -1;
        }
      } else {
        histRotate.revertAll();
        return this->accept(state, ctx);
      }
      break;
    case EditActionType::CANCEL:
      errno = EAGAIN;
      return -1;
    case EditActionType::COMPLETE:
      if (this->completionCallback) {
        auto s = this->completeLine(state, ctx, reader);
        if (s == EditActionStatus::ERROR) {
          return -1;
        } else if (s == EditActionStatus::CANCEL) {
          errno = EAGAIN;
          return -1;
        }
        if (!reader.empty()) {
          goto NO_FETCH;
        }
      }
      break;
    case EditActionType::BACKWARD_DELETE_CHAR:
      if (ctx.buf.deletePrevChar(nullptr, true)) {
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::DELETE_CHAR:
      if (ctx.buf.deleteNextChar(nullptr, true)) {
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::DELETE_OR_EXIT: /* remove char at right of cursor, or if the line is empty,
                                        act as end-of-file. */
      if (ctx.buf.getUsedSize() > 0) {
        if (ctx.buf.deleteNextChar(nullptr, true)) {
          this->refreshLine(state, ctx);
        }
      } else {
        errno = 0;
        return -1;
      }
      break;
    case EditActionType::TRANSPOSE_CHAR: /* swaps current character with previous */
      if (linenoiseEditSwapChars(ctx.buf)) {
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::BACKWARD_CHAR:
      if (ctx.buf.moveCursorToLeftByChar()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::FORWARD_CHAR:
      if (ctx.buf.moveCursorToRightByChar()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::PREV_HISTORY:
    case EditActionType::NEXT_HISTORY: {
      auto op = action->type == EditActionType::PREV_HISTORY ? HistRotator::Op::PREV
                                                             : HistRotator::Op::NEXT;
      if (rotateHistory(histRotate, prevRotating, ctx.buf, op, true)) {
        rotating = true;
        this->refreshLine(state, ctx);
      }
      break;
    }
    case EditActionType::UP_OR_HISTORY:
    case EditActionType::DOWN_OR_HISTORY: {
      auto op = action->type == EditActionType::UP_OR_HISTORY ? HistRotator::Op::PREV
                                                              : HistRotator::Op::NEXT;
      if (rotateHistoryOrUpDown(histRotate, ctx.buf, rotating, op, prevRotating)) {
        this->refreshLine(state, ctx);
      }
      break;
    }
    case EditActionType::BACKWARD_KILL_LINE: /* delete the whole line or delete to current */
      if (std::string capture; ctx.buf.deleteLineToCursor(false, &capture)) {
        this->killRing.add(std::move(capture));
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::KILL_LINE: /* delete from current to end of line */
      if (std::string capture; ctx.buf.deleteLineFromCursor(&capture)) {
        this->killRing.add(std::move(capture));
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::BEGINNING_OF_LINE: /* go to the start of the line */
      if (ctx.buf.moveCursorToStartOfLine()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::END_OF_LINE: /* go to the end of the line */
      if (ctx.buf.moveCursorToEndOfLine()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::BEGINNING_OF_BUF: /* go to the start of the buffer */
      if (ctx.buf.moveCursorToStartOfBuf()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::END_OF_BUF: /* go to the end of the buffer */
      if (ctx.buf.moveCursorToEndOfBuf()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::CLEAR_SCREEN:
      linenoiseClearScreen(this->outFd);
      this->refreshLine(state, ctx);
      break;
    case EditActionType::BACKWARD_KILL_WORD:
    BACKWARD_KILL_WORD_L:
      if (std::string capture; ctx.buf.deletePrevWord(&capture)) {
        this->killRing.add(std::move(capture));
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::KILL_WORD:
    KILL_WORD_L:
      if (std::string capture; ctx.buf.deleteNextWord(&capture)) {
        this->killRing.add(std::move(capture));
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::BACKWARD_WORD:
    BACKWARD_WORD_L:
      if (ctx.buf.moveCursorToLeftByWord()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::FORWARD_WORD:
    FORWARD_WORD_L:
      if (ctx.buf.moveCursorToRightByWord()) {
        this->refreshLine(state, ctx, false);
      }
      break;
    case EditActionType::BACKWARD_KILL_TOKEN:
      if (this->hasFeature(LineEditorFeature::LANG_EXTENSION)) {
        std::string capture;
        if (auto ret = deletePrevToken(ctx.buf, &capture, &ctx.tokenizeCache); ret.hasValue()) {
          if (ret.unwrap()) {
            this->killRing.add(std::move(capture));
            this->refreshLine(state, ctx);
          }
          break;
        }
      }
      goto BACKWARD_KILL_WORD_L;
    case EditActionType::KILL_TOKEN:
      if (this->hasFeature(LineEditorFeature::LANG_EXTENSION)) {
        std::string capture;
        if (auto ret = deleteNextToken(ctx.buf, &capture, &ctx.tokenizeCache); ret.hasValue()) {
          if (ret.unwrap()) {
            this->killRing.add(std::move(capture));
            this->refreshLine(state, ctx);
          }
          break;
        }
      }
      goto KILL_WORD_L;
    case EditActionType::BACKWARD_TOKEN:
      if (this->hasFeature(LineEditorFeature::LANG_EXTENSION)) {
        if (auto ret = moveCursorToLeftByToken(ctx.buf, &ctx.tokenizeCache); ret.hasValue()) {
          if (ret.unwrap()) {
            this->refreshLine(state, ctx, false);
          }
          break;
        }
      }
      goto BACKWARD_WORD_L;
    case EditActionType::FORWARD_TOKEN:
      if (this->hasFeature(LineEditorFeature::LANG_EXTENSION)) {
        if (auto ret = moveCursorToRightByToken(ctx.buf, &ctx.tokenizeCache); ret.hasValue()) {
          if (ret.unwrap()) {
            this->refreshLine(state, ctx, false);
          }
          break;
        }
      }
      goto FORWARD_WORD_L;
    case EditActionType::NEWLINE:
      if (ctx.buf.insertToCursor({"\n", 1})) {
        this->refreshLine(state, ctx);
      } else {
        return -1;
      }
      break;
    case EditActionType::YANK:
      if (this->killRing) {
        this->killRing.reset();
        StringRef line = this->killRing.getCurrent();
        if (!line.empty()) {
          yankedSize = line.size();
          if (ctx.buf.insertToCursor(line)) {
            this->refreshLine(state, ctx);
          } else {
            return -1;
          }
        }
      }
      break;
    case EditActionType::YANK_POP:
      if (prevYankedSize > 0) {
        assert(this->killRing);
        ctx.buf.undo();
        this->killRing.rotate();
        StringRef line = this->killRing.getCurrent();
        if (!line.empty()) {
          yankedSize = line.size();
          if (ctx.buf.insertToCursor(line)) {
            this->refreshLine(state, ctx);
          } else {
            return -1;
          }
        }
      }
      break;
    case EditActionType::UNDO:
      if (ctx.buf.undo()) {
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::REDO:
      if (ctx.buf.redo()) {
        this->refreshLine(state, ctx);
      }
      break;
    case EditActionType::INSERT_KEYCODE:
    REDO_INSERT_KEYCODE:
      if (ssize_t r = reader.fetch(toSigSet(state.sigVector)); r > 0) {
        if (reader.hasBracketedPasteStart()) {
          goto NO_FETCH; // not insert bracket start bytes
        }
        auto &buf = reader.get();
        if (const bool merge = buf != " " && buf != "\n"; ctx.buf.insertToCursor(buf, merge)) {
          this->refreshLine(state, ctx);
        } else {
          return -1;
        }
      } else if (r == -1) {
        if (errno == EINTR) {
          if (this->handleSignals(state)) {
            this->refreshLine(state, ctx);
            goto REDO_INSERT_KEYCODE;
          }
          if (state.hasError()) {
            errno = EAGAIN;
          }
        }
        return -1;
      }
      break;
    case EditActionType::CUSTOM: {
      bool r = this->kickCustomCallback(state, ctx.buf, action->customActionType,
                                        action->customActionIndex);
      this->refreshLine(state, ctx); // always refresh line even if error
      if (r && action->customActionType == CustomActionType::REPLACE_WHOLE_ACCEPT) {
        histRotate.revertAll();
        return this->accept(state, ctx);
      }
      if (state.hasError()) {
        errno = EAGAIN;
        return -1;
      }
      break;
    }
    }
  }
  return static_cast<ssize_t>(ctx.buf.getUsedSize());
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
ssize_t LineEditorObject::readline(ARState &state, StringRef prompt, char *buf, size_t bufLen) {
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

  state.incReadlineCallCount();
  this->lock = true;
  this->continueLine = false;
  auto cleanup = finally([&] {
    this->lock = false;
    state.declReadlineCallCount();
  });

  // check call count (not allow recursive readline call)
  if (state.getReadlineCallCount() > 1) {
    raiseError(state, TYPE::InvalidOperationError, "cannot call readline recursively");
    errno = EAGAIN;
    return -1;
  }

  // prepare prompt
  Value promptVal;
  if (this->promptCallback) {
    auto args = makeArgs(Value::createStr(prompt));
    Value callback = this->promptCallback;
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
  }
  PathLikeChecker pathLikeChecker(state);
  RenderingContext ctx(buf, bufLen, prompt, std::ref(pathLikeChecker));
  return this->editLine(state, ctx);
}

static bool insertCandidate(LineBuffer &buf, const StringRef inserting,
                            const CandidateAttr::Suffix suffix) {
  bool s = buf.insertToCursor(inserting, true);
  if (s) {
    StringRef suffixChar;
    switch (suffix) {
    case CandidateAttr::Suffix::NONE:
      break;
    case CandidateAttr::Suffix::SPACE:
      suffixChar = " ";
      break;
    case CandidateAttr::Suffix::PAREN:
      suffixChar = "(";
      break;
    case CandidateAttr::Suffix::PAREN_PAIR:
      suffixChar = "()";
      break;
    }
    if (!suffixChar.empty()) {
      s = buf.insertToCursor(suffixChar, true);
    }
  }
  buf.commitLastChange();
  return s;
}

EditActionStatus LineEditorObject::completeLine(ARState &state, RenderingContext &ctx,
                                                KeyCodeReader &reader) {
  reader.clear();

  CandidatesWrapper candidates(this->kickCompletionCallback(state, ctx.buf.getToCursor()));
  if (!candidates || candidates.size() <= 1) {
    this->refreshLine(state, ctx);
  }
  if (!candidates) {
    return EditActionStatus::CANCEL;
  }

  const auto watchSigSet = toSigSet(state.sigVector);
  unsigned int undoCount = 0;
  StringRef inserting = candidates.getCommonPrefixStr();
  ctx.buf.commitLastChange();
  const size_t offset = ctx.buf.resolveInsertingSuffix(inserting, candidates.size() == 1);
  if (const auto size = candidates.size(); size > 0) {
    const auto suffix = size == 1 ? candidates.getAttrAt(0).suffix : CandidateAttr::Suffix::NONE;
    if (insertCandidate(ctx.buf, inserting, suffix)) {
      this->refreshLine(state, ctx);
    } else {
      return EditActionStatus::ERROR;
    }
    if (!inserting.empty()) {
      undoCount++;
    }
  }
  if (const auto len = candidates.size(); len == 0) {
    linenoiseBeep(this->outFd);
    return EditActionStatus::OK;
  } else if (len == 1) {
    return EditActionStatus::OK;
  }

  // show candidates
  auto status = EditActionStatus::CONTINUE;
  auto pager = ArrayPager::create(CandidatesWrapper(candidates), ctx.ps, {});

  /**
   * first, only show pager and wait next completion action.
   * if next action is not completion action, break paging
   */
  pager.setShowCursor(false);

FIRST_DRAW:
  this->refreshLine(state, ctx, true, makeObserver(pager));
FETCH:
  if (ssize_t r = reader.fetch(watchSigSet); r <= 0) {
    if (r == -1 && errno == EINTR) {
      if (this->handleSignals(state)) {
        goto FIRST_DRAW;
      }
      if (state.hasError()) {
        status = EditActionStatus::CANCEL;
        goto END;
      }
    }
    status = EditActionStatus::ERROR;
    goto END;
  }
  if (!reader.hasControlChar()) {
    status = EditActionStatus::OK;
    goto END;
  }
  if (!reader.getEvent().hasValue()) {
    goto FETCH; // ignore unrecognized escape sequence
  }
  if (auto *action = this->keyBindings.findAction(reader.getEvent().unwrap());
      !action || action->type != EditActionType::COMPLETE) {
    status = EditActionStatus::OK;
    goto END;
  }

  /**
   * paging completion candidates
   */
  pager.setShowCursor(true);
  undoCount++;
  for (const unsigned int oldSize = ctx.buf.getUsedSize(); status == EditActionStatus::CONTINUE;) {
    // render pager
    if (oldSize != ctx.buf.getUsedSize()) {
      ctx.buf.undo();
    }
    const auto can = pager.getCurCandidate();
    const auto suffix = pager.getCurCandidateAttr().suffix;
    assert(offset <= ctx.buf.getCursor());
    size_t prefixLen = ctx.buf.getCursor() - offset;
    size_t prevCanLen = can.size() - prefixLen;
    if (insertCandidate(ctx.buf, {can.data() + prefixLen, prevCanLen}, suffix)) {
      this->refreshLine(state, ctx, true, makeObserver(pager));
    } else {
      status = EditActionStatus::ERROR;
      break;
    }
    status = waitPagerAction(pager, this->keyBindings, reader,  watchSigSet);
    if (status == EditActionStatus::REVERT) {
      status = EditActionStatus::OK;
      while (undoCount > 0) {
        ctx.buf.undo();
        undoCount--;
      }
      goto END;
    }
    if (status == EditActionStatus::CANCEL && errno == EINTR) {
      if (this->handleSignals(state)) {
        status = EditActionStatus::CONTINUE;
      }
      if (state.hasError()) {
        break;
      }
    }
  }

END:
  const int old = errno;
  this->refreshLine(state, ctx); // clear pager
  errno = old;
  return status;
}

Value LineEditorObject::kickCallback(ARState &state, Value &&callback, CallArgs &&callArgs) {
  const int errNum = errno;
  auto oldStatus = state.getGlobal(BuiltinVarOffset::EXIT_STATUS);
  auto oldIFS = state.getGlobal(BuiltinVarOffset::IFS);
  auto oldREPLY = state.getGlobal(BuiltinVarOffset::REPLY);
  auto oldReply = state.getGlobal(BuiltinVarOffset::REPLY_VAR);
  auto oldPipe = state.getGlobal(BuiltinVarOffset::PIPESTATUS);

  const bool restoreTTY = this->rawMode;
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
  state.setGlobal(BuiltinVarOffset::REPLY, std::move(oldREPLY));
  state.setGlobal(BuiltinVarOffset::REPLY_VAR, std::move(oldReply));
  state.setGlobal(BuiltinVarOffset::PIPESTATUS, std::move(oldPipe));
  errno = errNum;
  return ret;
}

ObjPtr<ArrayObject> LineEditorObject::kickCompletionCallback(ARState &state, StringRef line) {
  assert(this->completionCallback);

  const auto &modType = getCurRuntimeModule(state);
  auto mod = state.getGlobal(modType.getIndex());
  auto args = makeArgs(std::move(mod), Value::createStr(line));
  Value callback = this->completionCallback;
  const auto ret = this->kickCallback(state, std::move(callback), std::move(args));
  if (state.hasError()) {
    return nullptr;
  }
  return toObjPtr<ArrayObject>(ret);
}

bool LineEditorObject::kickHistSyncCallback(ARState &state, const LineBuffer &buf) {
  if (!this->history) {
    return true;
  }
  if (this->histSyncCallback) {
    this->kickCallback(state, this->histSyncCallback,
                       makeArgs(Value::createStr(buf.get()), this->history));
    return !state.hasError();
  }
  return this->history->append(state, Value::createStr(buf.get()));
}

static ObjPtr<ArrayObject> toObj(const TypePool &pool, const KillRing &killRing) {
  auto obj = toObjPtr<ArrayObject>(Value::create<ArrayObject>(pool.get(TYPE::StringArray)));
  const auto &buf = killRing.get();
  const unsigned int size = buf.size();
  for (unsigned int i = 0; i < size; i++) {
    obj->append(Value::createStr(buf[i])); // not check iterator invalidation
  }
  return obj;
}

bool LineEditorObject::kickCustomCallback(ARState &state, LineBuffer &buf, CustomActionType type,
                                          unsigned int index) {
  StringRef line;
  auto optArg = Value::createInvalid();
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
    optArg = toObj(state.typePool, this->killRing);
    break;
  }

  auto iter = this->lookupCustomCallback(index);
  assert(iter != this->customCallbacks.end());
  const auto ret =
      this->kickCallback(state, Value(*iter), makeArgs(Value::createStr(line), std::move(optArg)));
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
  return buf.insertToCursor(ret.asStrRef());
}

bool LineEditorObject::handleSignals(ARState &state) {
  errno = 0; // clear EINTR
  if (ARState::hasSignal(SIGWINCH)) {
    if (!state.sigVector.lookup(SIGWINCH)) {
      ARState::clearPendingSignal(SIGWINCH);
      if (!ARState::hasSignals()) { // if received signal is only SIGWINCH, just refresh.
        return true;
      }
    }
  }

  auto func = getBuiltinGlobal(state, VAR_SIG_IGN); // dummy function
  auto args = makeArgs(Value::createSig(SIGHUP));   // dummy
  /**
   * implicitly call signal handler via dummy function call
   */
  this->kickCallback(state, std::move(func), std::move(args));
  return !state.hasError();
}

Value LineEditorObject::getkey(ARState &state) {
  int errNum = 0;
  Value ret;

  KeyCodeReader reader(this->inFd);
  if (this->enableRawMode(this->inFd) == 0 && reader.fetch() >= 0) {
    auto typeOrError = state.typePool.createTupleType(
        {&state.typePool.get(TYPE::String), &state.typePool.get(TYPE::String)});
    assert(typeOrError && typeOrError.asOk()->isTupleType());
    auto &tupleType = cast<TupleType>(*typeOrError.asOk());
    ret = Value::create<BaseObject>(tupleType);
    auto &obj = typeAs<BaseObject>(ret);
    obj[0] = Value::createStr(KeyEvent::toCaret(reader.get()));
    std::string event;
    if (reader.getEvent().hasValue()) {
      event = reader.getEvent().unwrap().toString();
    }
    obj[1] = Value::createStr(std::move(event));
  } else {
    errNum = errno;
  }

  // force consume remain bytes
  for (char data[256]; readRetryWithTimeout(this->inFd, data, std::size(data), 10) != -2;)
    ;

  this->disableRawMode(this->inFd);
  if (errNum) {
    raiseSystemError(state, errNum, "cannot read keycode");
  }
  return ret;
}

} // namespace arsh