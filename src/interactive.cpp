/*
 * Copyright (C) 2015-2020 Nagisa Sekiguchi
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

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <linenoise.h>

#include "misc/resource.hpp"
#include "misc/unicode.hpp"
#include <ydsh/ydsh.h>

static DSState *state;

/**
 * line is not nullptr
 */
static bool isSkipLine(const ydsh::CStrPtr &line) {
  for (const char *ptr = line.get(); *ptr != '\0'; ptr++) {
    switch (*ptr) {
    case ' ':
    case '\t':
    case '\r':
    case '\n':
      break;
    default:
      return false;
    }
  }
  return true;
}

/**
 * line is not nullptr
 */
static bool checkLineContinuation(const ydsh::CStrPtr &line) {
  const char *begin = line.get();
  unsigned int count = 0;
  for (const char *ptr = begin + strlen(begin) - 1; ptr != begin && *ptr == '\\'; ptr--) {
    count++;
  }
  return count % 2 != 0;
}

static void addHistory(const char *line) {
  DSLineEdit edit{};
  edit.data = line;
  DSState_lineEdit(state, DS_EDIT_HIST_ADD, &edit);
}

static void loadHistory() {
  DSLineEdit edit{};
  DSState_lineEdit(state, DS_EDIT_HIST_LOAD, &edit);
}

static void saveHistory() {
  DSLineEdit edit{};
  DSState_lineEdit(state, DS_EDIT_HIST_SAVE, &edit);
}

static const char *prompt(unsigned int n) {
  DSLineEdit edit{};
  edit.index = n;
  DSState_lineEdit(state, DS_EDIT_PROMPT, &edit);
  return edit.data;
}

static std::vector<std::string> notifyQueue;

static int notifyCallback(DSNotifyKind, const char *message) {
  notifyQueue.push_back(message);
  return 0;
}

static void showNotification() {
  for (auto &e : notifyQueue) {
    fputs(e.c_str(), stderr);
    fflush(stderr);
  }
  notifyQueue.clear();
}

static const std::string *lineBuf = nullptr;

static bool readLine(std::string &line) {
  line.clear();
  lineBuf = &line;

  bool continuation = false;
  while (true) {
    if (!continuation) {
      showNotification();
    }

    errno = 0;
    auto str = ydsh::CStrPtr(linenoise(prompt(continuation ? 2 : 1)));
    if (str == nullptr) {
      if (errno == EAGAIN) {
        continuation = false;
        line.clear();
        continue;
      }
      if (DSState_mode(state) != DS_EXEC_MODE_NORMAL) {
        return false;
      }
      line = "exit\n";
      return true;
    }

    if (isSkipLine(str)) {
      continue;
    }
    line += str.get();
    continuation = checkLineContinuation(str);
    if (continuation) {
      line.pop_back(); // remove '\\'
      continue;
    }
    break;
  }

  addHistory(line.c_str());
  line += '\n'; // terminate newline
  return true;
}

// for linenoise encoding function
using namespace ydsh;

struct WidthProperty {
  bool fullWidth{false};
  unsigned char flagSeqWidth{4};
  bool zwjSeqFallback{true};
};

static WidthProperty widthProperty;

static constexpr const char PROPERTY_EAW[] = "○";
static constexpr const char PROPERTY_EMOJI_FLAG_SEQ[] = "🇯🇵";
static constexpr const char PROPERTY_EMOJI_ZWJ_SEQ[] = "👩🏼‍🏭";
static const char *propertyStr[] = {
    PROPERTY_EAW,
    PROPERTY_EMOJI_FLAG_SEQ,
    PROPERTY_EMOJI_ZWJ_SEQ,
};

static int checkProperty(const char *str, size_t pos) {
  if (strcmp(str, PROPERTY_EAW) == 0) {
    widthProperty.fullWidth = false;
    if (pos - 1 == 2) {
      widthProperty.fullWidth = true;
    }
  } else if (strcmp(str, PROPERTY_EMOJI_FLAG_SEQ) == 0) {
    widthProperty.flagSeqWidth = pos - 1;
  } else if (strcmp(str, PROPERTY_EMOJI_ZWJ_SEQ) == 0) {
    widthProperty.zwjSeqFallback = pos - 1 > 2;
  }
  return 0;
}

static void setWidthProperty(DSLineEdit &edit) {
  if (widthProperty.fullWidth) {
    DSLineEdit_setFullWidth(&edit);
  }
  DSLineEdit_setFlagSeqWidth(&edit, widthProperty.flagSeqWidth);
  if (widthProperty.zwjSeqFallback) {
    DSLineEdit_setZWJFallback(&edit);
  }
}

static std::size_t encoding_nextCharLen(const char *buf, std::size_t bufSize, std::size_t pos,
                                        std::size_t *columSize) {
  DSLineEdit edit{};
  edit.data = buf + pos;
  edit.index = bufSize - pos;
  setWidthProperty(edit);
  DSState_lineEdit(state, DS_EDIT_NEXT_CHAR_LEN, &edit);
  if (columSize) {
    *columSize = edit.out2;
  }
  return edit.out;
}

static std::size_t encoding_prevCharLen(const char *buf, std::size_t, std::size_t pos,
                                        std::size_t *columSize) {
  DSLineEdit edit{};
  edit.data = buf;
  edit.index = pos;
  setWidthProperty(edit);
  DSState_lineEdit(state, DS_EDIT_PREV_CHAR_LEN, &edit);
  if (columSize) {
    *columSize = edit.out2;
  }
  return edit.out;
}

static std::size_t encoding_readCode(int fd, char *buf, std::size_t bufSize, int *codePoint) {
  if (bufSize < 1) {
    return -1;
  }

  ssize_t readSize = read(fd, &buf[0], 1);
  if (readSize <= 0) {
    return readSize;
  }

  unsigned int byteSize = UnicodeUtil::utf8ByteSize(buf[0]);
  if (byteSize < 1 || byteSize > 4) {
    return -1;
  }

  if (byteSize > 1) {
    if (bufSize < byteSize) {
      return -1;
    }
    readSize = read(fd, &buf[1], byteSize - 1);
    if (readSize <= 0) {
      return readSize;
    }
  }
  return UnicodeUtil::utf8ToCodePoint(buf, bufSize, *codePoint);
}

static void completeCallback(const char *buf, size_t cursor, linenoiseCompletions *lc) {
  std::string actualBuf(*lineBuf);
  size_t actualCursor = actualBuf.size() + cursor;
  actualBuf += buf;
  actualBuf += '\n';

  const char *line = actualBuf.c_str();
  int ret = DSState_complete(state, line, actualCursor);
  lc->attr = 0;
  lc->len = ret > -1 ? static_cast<unsigned int>(ret) : 0;
  if (ret < 0 && errno == EINTR) {
    lc->attr |= LINENOISE_COMPLETION_ATTR_INTR;
  }
  lc->cvec = static_cast<char **>(malloc(sizeof(char *) * lc->len));
  DSCompletion comp{};
  for (unsigned int i = 0; i < lc->len; i++) {
    comp = DSCompletion{};
    DSState_getCompletion(state, i, &comp);
    lc->cvec[i] = strdup(comp.value);
  }
  if (lc->len == 1 && DSCompletion_isNoSpace(&comp)) {
    lc->attr |= LINENOISE_COMPLETION_ATTR_NOSP;
  }
}

static const char *historyCallback(const char *buf, int *historyIndex, historyOp op) {
  DSLineEdit edit{};
  DSState_lineEdit(state, DS_EDIT_HIST_SIZE, &edit);
  const unsigned int size = edit.out;
  switch (op) {
  case LINENOISE_HISTORY_OP_NEXT:
  case LINENOISE_HISTORY_OP_PREV: {
    if (size > 1) {
      edit.data = buf;
      edit.index = size - *historyIndex - 1;
      DSState_lineEdit(state, DS_EDIT_HIST_SET, &edit);
      *historyIndex += (op == LINENOISE_HISTORY_OP_PREV) ? 1 : -1;
      if (*historyIndex < 0) {
        *historyIndex = 0;
        return nullptr;
      }
      if (static_cast<unsigned int>(*historyIndex) >= size) {
        *historyIndex = size - 1;
        return nullptr;
      }
      edit.data = nullptr;
      edit.index = size - *historyIndex - 1;
      DSState_lineEdit(state, DS_EDIT_HIST_GET, &edit);
      return edit.data;
    }
    break;
  }
  case LINENOISE_HISTORY_OP_DELETE:
    edit.index = size - 1;
    DSState_lineEdit(state, DS_EDIT_HIST_DEL, &edit);
    break;
  case LINENOISE_HISTORY_OP_INIT:
    DSState_lineEdit(state, DS_EDIT_HIST_INIT, &edit);
    break;
  case LINENOISE_HISTORY_OP_SEARCH:
    edit.data = buf;
    edit.index = size - *historyIndex - 1;
    DSState_lineEdit(state, DS_EDIT_HIST_SEARCH, &edit);
    return edit.data;
  }
  return nullptr;
}

static const char *highlight(const char *buf, size_t bufLen, size_t *retLen) {
  DSLineEdit edit{};
  edit.data = buf;
  edit.index = bufLen;
  DSState_lineEdit(state, DS_EDIT_HIGHLIGHT, &edit);
  *retLen = edit.out;
  return edit.data;
}

static std::pair<DSErrorKind, int> loadRC(const std::string &rcfile) {
  if (rcfile.empty()) { // for --norc option
    return {DS_ERROR_KIND_SUCCESS, 0};
  }

  DSError e{};
  int ret = DSState_loadModule(state, rcfile.c_str(), DS_MOD_FULLPATH | DS_MOD_IGNORE_ENOENT, &e);
  auto kind = e.kind;
  DSError_release(&e);

  // reset line num
  DSState_setLineNum(state, 1);

  return {kind, ret};
}

int exec_interactive(DSState *dsState, const std::string &rcfile) {
  state = dsState;

  *linenoiseInputFD() = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
  *linenoiseOutputFD() = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
  *linenoiseErrorFD() = *linenoiseOutputFD();

  linenoiseSetEncodingFunctions(encoding_prevCharLen, encoding_nextCharLen, encoding_readCode);

  linenoiseSetMultiLine(1);

  linenoiseSetCompletionCallback(completeCallback);

  linenoiseSetHistoryCallback(historyCallback);

  linenoiseSetPropertyCheckCallback(checkProperty, propertyStr, std::size(propertyStr));

  linenoiseSetHighlightCallback(highlight);

  unsigned int option = DS_OPTION_JOB_CONTROL | DS_OPTION_INTERACTIVE;
  DSState_setOption(dsState, option);

  auto ret = loadRC(rcfile);
  if (ret.first != DS_ERROR_KIND_SUCCESS) {
    return ret.second;
  }

  loadHistory();

  DSState_watchNotification(dsState, notifyCallback);

  int status = 0;
  for (std::string line; readLine(line);) {
    DSError e; // NOLINT
    status = DSState_eval(dsState, nullptr, line.c_str(), line.size(), &e);
    auto kind = e.kind;
    DSError_release(&e);
    if (kind == DS_ERROR_KIND_EXIT || kind == DS_ERROR_KIND_ASSERTION_ERROR) {
      break;
    }
  }
  saveHistory();
  return status;
}
