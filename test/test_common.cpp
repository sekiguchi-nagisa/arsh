/*
 * Copyright (C) 2017-2018 Nagisa Sekiguchi
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

#include <cstdarg>

#include "test_common.h"

std::string format(const char *fmt, ...) {
  va_list arg;

  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("");
  }
  va_end(arg);

  std::string v = str;
  free(str);
  return v;
}

// #######################
// ##     Extractor     ##
// #######################

void Extractor::consumeSpace() {
  for (; *this->str != '\0'; this->str++) {
    if (!isspace(*this->str)) {
      return;
    }
  }
}

int Extractor::extract(unsigned int &value) {
  std::string buf;
  for (; *this->str != '\0'; this->str++) {
    int ch = static_cast<unsigned char>(*this->str);
    if (!isdigit(ch)) {
      break;
    }
    buf += static_cast<char>(ch);
  }
  auto v = std::stoll(buf);
  if (v < 0 || v > UINT32_MAX) {
    return 1;
  }
  value = static_cast<unsigned int>(v);
  return 0;
}

int Extractor::extract(int &value) {
  std::string buf;
  for (; *this->str != '\0'; this->str++) {
    int ch = static_cast<unsigned char>(*this->str);
    if (!isdigit(ch)) {
      break;
    }
    buf += static_cast<char>(ch);
  }
  value = std::stoi(buf);
  return 0;
}

int Extractor::extract(std::string &value) {
  value.clear();

  if (*this->str != '"') {
    return 1;
  }
  this->str++;

  for (; *this->str != '\0'; this->str++) {
    char ch = *this->str;
    if (ch == '"') {
      this->str++;
      return 0;
    }
    if (ch == '\\') {
      char next = *(this->str + 1);
      if (next == '\\' || next == '"') {
        ch = next;
        this->str++;
      }
    }
    value += ch;
  }
  return 1;
}

int Extractor::extract(const char *value) {
  this->consumeSpace();

  auto size = strlen(value);
  if (strncmp(this->str, value, size) != 0) {
    return 1;
  }
  this->str += size;
  return 0;
}

// #############################
// ##     InteractiveBase     ##
// #############################

void InteractiveBase::invokeImpl(const std::vector<std::string> &args) {
  termios term;
  xcfmakesane(term);
  auto builder = ProcBuilder{this->binPath.c_str()}
                     .addArgs(args)
                     .setWorkingDir(this->workingDir.c_str())
                     .setIn(IOConfig::PTY)
                     .setOut(IOConfig::PTY)
                     .setErr(IOConfig::PIPE)
                     .setWinSize(24, 200)
                     .setTerm(term);
  for (auto &e : this->envMap) {
    builder.addEnv(e.first.c_str(), e.second.c_str());
  }
  this->handle = builder();
}

void InteractiveShellBase::interpret(std::string &line) {
  Screen screen(this->handle.getWinSize());
  screen.setReporter([&](std::string &&m) { this->send(m.c_str()); });
  screen.interpret(line.c_str(), line.size());
  line = screen.toString();
}

std::pair<std::string, std::string> InteractiveShellBase::readAll() {
  std::string err;
  Screen screen(this->handle.getWinSize());
  screen.setEAW(2);
  screen.setReporter([&](std::string &&m) { this->send(m.c_str()); });
  this->handle.readAll(this->timeout, [&](unsigned int index, const char *buf, unsigned int size) {
    if (index == 0) {
      screen.interpret(buf, size);
    } else {
      err.append(buf, size);
    }
  });
  return {screen.toString(), std::move(err)};
}