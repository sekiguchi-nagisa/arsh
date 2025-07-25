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

#ifndef ARSH_TEST_COMMON_H
#define ARSH_TEST_COMMON_H

#include <chrono>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "misc/files.hpp"
#include <ansi.h>
#include <process.h>

#include "../tools/platform/platform.h"

using namespace process;

// common utility for test

std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

inline std::string makeLineMarker(const std::string &line) {
  std::string str = "^";
  for (unsigned int i = 1; i < line.size(); i++) {
    str += "~";
  }
  return str;
}

inline std::vector<std::string> split(const std::string &str, int delim) {
  std::vector<std::string> bufs;
  bufs.emplace_back();

  for (auto &ch : str) {
    if (ch == delim) {
      bufs.emplace_back();
    } else {
      bufs.back() += ch;
    }
  }
  return bufs;
}

class Extractor {
private:
  const char *str;

public:
  explicit Extractor(const char *str) : str(str) {}

  template <typename... Arg>
  int operator()(Arg &&...arg) {
    return this->delegate(std::forward<Arg>(arg)...);
  }

private:
  void consumeSpace();

  int extract(unsigned int &value);

  int extract(int &value);

  int extract(std::string &value);

  int extract(const char *value);

  int delegate() {
    this->consumeSpace();
    return strlen(this->str);
  }

  template <typename F, typename... T>
  int delegate(F &&first, T &&...rest) {
    int ret = this->extract(std::forward<F>(first));
    return ret == 0 ? this->delegate(std::forward<T>(rest)...) : ret;
  }
};

struct ExpectOutput : public ::testing::Test {
  void expect(const Output &output, int status = 0, WaitStatus::Kind type = WaitStatus::EXITED,
              const std::string &out = "", const std::string &err = "") {
    EXPECT_EQ(status, output.status.value);
    EXPECT_EQ(type, output.status.kind);
    EXPECT_EQ(out, output.out);
    EXPECT_EQ(err, output.err);
    ASSERT_FALSE(this->HasFailure());
  }

  void expect(ProcBuilder &&builder, int status, const std::string &out = "",
              const std::string &err = "") {
    auto result = builder.execAndGetResult(false);
    ASSERT_NO_FATAL_FAILURE(this->expect(result, status, WaitStatus::EXITED, out, err));
  }

  void expectRegex(const Output &output, int status, WaitStatus::Kind type, const std::string &out,
                   const std::string &err = "") {
    EXPECT_EQ(status, output.status.value);
    EXPECT_EQ(type, output.status.kind);
    EXPECT_THAT(output.out, ::testing::MatchesRegex(out));
    EXPECT_THAT(output.err, ::testing::MatchesRegex(err));
    ASSERT_FALSE(this->HasFailure());
  }

  void expectRegex(ProcBuilder &&builder, int status, const std::string &out,
                   const std::string &err = "") {
    auto result = builder.execAndGetResult(false);
    ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, status, WaitStatus::EXITED, out, err));
  }
};

class InteractiveBase : public ExpectOutput {
protected:
  static constexpr unsigned int DEFAULT_WIN_ROW = 24;
  static constexpr unsigned int DEFAULT_WIN_COL = 200;

  int timeoutMSec{80};

  ProcHandle handle;

  const std::string binPath;

  const std::string workingDir;

  std::unordered_map<std::string, std::string> envMap;

public:
  InteractiveBase(const char *binPath, const char *dir) : binPath(binPath), workingDir(dir) {
    this->addEnv("TERM", "xterm");
  }

  template <typename... T>
  void invoke(T &&...args) {
    std::vector<std::string> values = {std::forward<T>(args)...};
    int sleepTimeMSec = 100;
    switch (arsh::platform::platform()) {
    case arsh::platform::PlatformType::DARWIN:
    case arsh::platform::PlatformType::CYGWIN:
    case arsh::platform::PlatformType::MSYS:
      sleepTimeMSec = 200;
      break;
    default:
      break;
    }
    this->invokeImpl(values, sleepTimeMSec);
  }

  void invokeImpl(const std::vector<std::string> &args, int sleepMSec = 200,
                  bool mergeErrToOut = false);

  void addEnv(const char *name, const char *value) { this->envMap.emplace(name, value); }

  void send(arsh::StringRef ref) {
    int r = write(this->handle.in(), ref.data(), ref.size());
    (void)r;
    fsync(this->handle.in());
  }

  virtual std::pair<std::string, std::string> readAll() {
    return this->handle.readAll(this->timeoutMSec);
  }

  [[nodiscard]] auto withTimeout(int msec) {
    auto old = this->timeoutMSec;
    this->timeoutMSec = msec;
    return arsh::finally([&, old] { this->timeoutMSec = old; });
  }

  void expectRegex(const std::string &out = "", const std::string &err = "") {
    auto pair = this->readAll();
    EXPECT_THAT(pair.first, ::testing::MatchesRegex(out));
    EXPECT_THAT(pair.second, ::testing::MatchesRegex(err));
    ASSERT_FALSE(this->HasFailure());
  }

  void expect(const std::string &out = "", const std::string &err = "") {
    auto pair = this->readAll();
    EXPECT_EQ(out, pair.first);
    EXPECT_EQ(err, pair.second);
    ASSERT_FALSE(this->HasFailure());
  }

  void waitAndExpect(int status = 0, WaitStatus::Kind type = WaitStatus::EXITED,
                     const std::string &out = "", const std::string &err = "") {
    auto pair = this->readAll();
    auto s = this->handle.wait();
    auto ret = Output{.status = s, .out = std::move(pair.first), .err = std::move(pair.second)};
    ASSERT_NO_FATAL_FAILURE(ExpectOutput::expect(ret, status, type, out, err));
  }
};

class InteractiveShellBase : public InteractiveBase {
protected:
  std::string prompt{"> "};
  bool resetBeforeRead{true};
  arsh::AmbiguousCharWidth eaw{arsh::AmbiguousCharWidth::FULL};

private:
  std::function<void()> bellCallback;
  std::function<void(arsh::StringRef)> csiListener;
  Screen screen;

public:
  InteractiveShellBase(const char *binPath, const char *dir)
      : InteractiveBase(binPath, dir), screen({.row = DEFAULT_WIN_ROW, .col = DEFAULT_WIN_COL}) {
    this->screen.setEAW(this->eaw);
  }

  void setPrompt(const std::string &p) { this->prompt = p; }

  void setBellCallback(std::function<void()> &&callback) {
    this->bellCallback = std::move(callback);
  }

  void setCSIListener(std::function<void(arsh::StringRef)> &&callback) {
    this->csiListener = std::move(callback);
  }

  std::pair<std::string, std::string> readAll() override;

  void sendLine(const char *line) {
    this->send(line);
    this->send("\r");
  }

  void sendLineAndExpect(const char *line, const char *out = "", const char *err = "") {
    this->sendLine(line);

    std::string eout;
    if (strlen(line) != 0) {
      eout = this->prompt;
    }
    eout += line;
    eout += "\n";
    eout += out;
    if (strlen(out) != 0) {
      eout += "\n";
    }
    eout += this->prompt;
    ASSERT_NO_FATAL_FAILURE(this->expect(eout, err));
  }

  void sendLineAndExpectRegex(const char *line, const std::string &out, const std::string &err) {
    this->sendLine(line);

    std::string eout;
    if (strlen(line) != 0) {
      eout = this->prompt;
    }
    eout += line;
    eout += "\n";
    eout += out;
    if (!out.empty()) {
      eout += "\n";
    }
    eout += this->prompt;

    auto pair = this->readAll();
    EXPECT_EQ(pair.first, eout);
    EXPECT_THAT(pair.second, ::testing::MatchesRegex(err));
    ASSERT_FALSE(this->HasFailure());
  }

  void sendLineAndWait(const char *line, int status = 0, WaitStatus::Kind type = WaitStatus::EXITED,
                       const char *out = "", const char *err = "") {
    this->send(line);
    this->send("\r");

    std::string eout = this->prompt;
    eout += line;
    eout += "\n";
    eout += out;
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(status, type, eout, err));
  }

  /**
   * for bracketed paste
   * @param data
   */
  void paste(arsh::StringRef data) {
    std::string str;
    str += "\x1b[200~";
    str += data;
    str += "\x1b[201~";
    this->send(str);
  }

  static constexpr unsigned char SHIFT = 1u << 1u;
  static constexpr unsigned char ALT = 1u << 2u;
  static constexpr unsigned char CTRL = 1u << 3u;

  void sendCSIu(int codePoint, unsigned char modifiers = {}) {
    std::string seq = "\x1b[";
    seq += std::to_string(codePoint);
    if (modifiers) {
      seq += ";";
      seq += std::to_string(modifiers);
    }
    seq += 'u';
    this->send(seq);
  }

private:
  void resetScreen();

protected:
  [[nodiscard]] auto reuseScreen() {
    this->resetBeforeRead = false;
    return arsh::finally([&] { this->resetBeforeRead = true; });
  }

  void changeWinSize(arsh::WinSize winSize) {
    if (this->handle.setWinSize(winSize)) {
      this->screen.resize({.row = winSize.rows, .col = winSize.cols});
    }
  }
};

#define INIT_TEMP_FILE_FACTORY(NAME) arsh::TempFileFactory("arsh_" #NAME)

#endif // ARSH_TEST_COMMON_H
