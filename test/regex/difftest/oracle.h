/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_TEST_REGEX_DIFFTEST_ORACLE_H
#define ARSH_TEST_REGEX_DIFFTEST_ORACLE_H

#include <map>
#include <string>
#include <vector>

#include "difftest.h"

namespace arsh::difftest {

/**
 * Client for the Node.js oracle (`oracle.js`), which runs the regexp engine used by Node.js (V8)
 * as the reference implementation of the ECMAScript RegExp semantics.
 *
 * A single `node` process is started and kept alive for the whole run: every case is one JSON line
 * written to its stdin and one JSON line read back from its stdout. Starting Node.js once instead
 * of once per case keeps a full corpus run in the seconds range instead of the minutes range.
 *
 * The process reports its `node` / `unicode` version and the syntax features it supports in a
 * banner before answering anything (see `oracle.js`), because the harness has to know them up
 * front: the Unicode version decides whether a version dependent case may be compared at all, and
 * the feature list decides whether a case is skipped.
 */
class Oracle {
public:
  struct Capabilities {
    std::map<std::string, bool> features;

    bool has(const std::string &name) const {
      auto iter = this->features.find(name);
      return iter != this->features.end() && iter->second;
    }
  };

private:
  pid_t pid{-1};
  int inFD{-1};
  int outFD{-1};

  std::string nodeVersion;
  std::string unicodeVersion;
  Capabilities capabilities;

  std::string readBuf;
  uint64_t nextId{0};

public:
  NON_COPYABLE(Oracle);

  Oracle() = default;

  ~Oracle();

  /**
   * Start `node <scriptPath>` and read the banner.
   *
   * @param nodePath the Node.js executable
   * @return false on failure, with the reason in `err`
   */
  bool start(const char *nodePath, const char *scriptPath, std::string &err);

  bool isStarted() const { return this->pid > -1; }

  const auto &getNodeVersion() const { return this->nodeVersion; }

  const auto &getUnicodeVersion() const { return this->unicodeVersion; }

  const auto &getCapabilities() const { return this->capabilities; }

  /**
   * Ask the oracle about one case.
   *
   * @return the normalized result; a case the oracle cannot answer (crash, protocol error) is
   * reported as a runtime error rather than a divergence
   */
  MatchResult match(const Case &c);

private:
  bool readLine(std::string &line, std::string &err);

  bool writeAll(const std::string &data);

  json::JSON request(const Case &c, std::string &err);
};

} // namespace arsh::difftest

#endif // ARSH_TEST_REGEX_DIFFTEST_ORACLE_H
