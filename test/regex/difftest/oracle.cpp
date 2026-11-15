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

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iterator>
#include <limits>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

#include <misc/fatal.h>

#include "oracle.h"

namespace arsh::difftest {

// ############################
// ##     NodeVersion     ##
// ############################

NodeVersion NodeVersion::parse(const StringRef version) {
  NodeVersion ret;
  const char *iter = version.begin();
  const char *const end = version.end();
  if (iter != end && *iter == 'v') { // `node --version` prints a `v` prefix, the banner does not
    ++iter;
  }

  uint32_t *const fields[] = {&ret.major, &ret.minor, &ret.patch};
  for (size_t i = 0; i < std::size(fields); i++) {
    const char *const begin = iter;
    uint32_t value = 0;
    while (iter != end && *iter >= '0' && *iter <= '9') {
      if (value <= (std::numeric_limits<uint32_t>::max() - 9) / 10) {
        value = value * 10 + static_cast<uint32_t>(*iter - '0');
      }
      ++iter;
    }
    if (iter == begin) { // no (more) numeric component
      break;
    }
    *fields[i] = value;
    if (i == 0) {
      ret.valid = true;
    }
    // the next component follows only if there is a dot right after this one; anything else
    // (a pre-release / build suffix, the end of the string) ends the version
    if (iter == end || *iter != '.' || i + 1 == std::size(fields)) {
      break;
    }
    ++iter;
  }
  return ret;
}

std::string NodeVersion::toString() const {
  std::string ret = std::to_string(this->major) + '.' + std::to_string(this->minor) + '.' +
                    std::to_string(this->patch);
  return ret;
}

bool operator<(const NodeVersion &x, const NodeVersion &y) {
  if (x.major != y.major) {
    return x.major < y.major;
  }
  if (x.minor != y.minor) {
    return x.minor < y.minor;
  }
  return x.patch < y.patch;
}

Oracle::~Oracle() { this->shutdown(); }

void Oracle::shutdown() {
  if (this->inFD > -1) {
    close(this->inFD);
    this->inFD = -1;
  }
  if (this->pid > -1) {
    // the oracle exits once its stdin reaches EOF; wait a little, then make sure it is gone
    for (unsigned int i = 0; i < 200; i++) {
      int status = 0;
      const pid_t ret = waitpid(this->pid, &status, WNOHANG);
      if (ret == this->pid || (ret < 0 && errno != EINTR)) {
        this->pid = -1;
        break;
      }
      usleep(1000);
    }
    if (this->pid > -1) {
      kill(this->pid, SIGKILL);
      int status = 0;
      while (waitpid(this->pid, &status, 0) < 0 && errno == EINTR) {
      }
      this->pid = -1;
    }
  }
  if (this->outFD > -1) {
    close(this->outFD);
    this->outFD = -1;
  }
  this->readBuf.clear();
}

bool Oracle::writeAll(const std::string &data) {
  // if the oracle died the pipe is closed on the other end; ignore SIGPIPE around the write so the
  // failure surfaces as EPIPE and the case is reported instead of killing the whole test binary
  void (*oldSigpipe)(int) = signal(SIGPIPE, SIG_IGN);
  const char *iter = data.data();
  size_t size = data.size();
  bool ok = true;
  while (size > 0) {
    const ssize_t written = write(this->inFD, iter, size);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      ok = false;
      break;
    }
    if (written == 0) {
      ok = false;
      break;
    }
    iter += written;
    size -= static_cast<size_t>(written);
  }
  signal(SIGPIPE, oldSigpipe);
  return ok;
}

bool Oracle::readLine(std::string &line, std::string &err) {
  while (true) {
    if (auto pos = this->readBuf.find('\n'); pos != std::string::npos) {
      line = this->readBuf.substr(0, pos);
      this->readBuf.erase(0, pos + 1);
      return true;
    }
    char buf[4096];
    const ssize_t size = read(this->outFD, buf, std::size(buf));
    if (size < 0) {
      if (errno == EINTR) {
        continue;
      }
      err = strerror(errno);
      return false;
    }
    if (size == 0) {
      err = "the oracle closed its output";
      return false;
    }
    this->readBuf.append(buf, static_cast<size_t>(size));
  }
}

bool Oracle::start(const char *nodePath, const char *scriptPath, std::string &err) {
  int toChild[2];
  int fromChild[2];
  if (pipe(toChild) < 0) {
    err = strerror(errno);
    return false;
  }
  if (pipe(fromChild) < 0) {
    err = strerror(errno);
    close(toChild[0]);
    close(toChild[1]);
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    err = strerror(errno);
    close(toChild[0]);
    close(toChild[1]);
    close(fromChild[0]);
    close(fromChild[1]);
    return false;
  }
  if (pid == 0) { // child
    dup2(toChild[0], STDIN_FILENO);
    dup2(fromChild[1], STDOUT_FILENO);
    close(toChild[0]);
    close(toChild[1]);
    close(fromChild[0]);
    close(fromChild[1]);
    execlp(nodePath, nodePath, scriptPath, static_cast<char *>(nullptr));
    _exit(127);
  }

  close(toChild[0]);
  close(fromChild[1]);
  this->pid = pid;
  this->inFD = toChild[1];
  this->outFD = fromChild[0];

  std::string line;
  if (!this->readLine(line, err)) {
    this->shutdown();
    return false;
  }
  auto json = json::JSON::fromString(line.c_str());
  if (!json.isObject()) {
    return this->fail(err, "invalid banner from the oracle: " + line);
  }
  const auto &obj = json.asObject();
  if (auto iter = obj.find("status");
      iter == obj.end() || !iter->second.isString() || iter->second.asString() != "hello") {
    return this->fail(err, "unexpected banner from the oracle: " + line);
  }
  if (auto iter = obj.find("node"); iter != obj.end() && iter->second.isString()) {
    this->nodeVersion = iter->second.asString();
  }
  if (auto iter = obj.find("unicode"); iter != obj.end() && iter->second.isString()) {
    this->unicodeVersion = iter->second.asString();
  }
  if (auto iter = obj.find("capabilities"); iter != obj.end() && iter->second.isObject()) {
    for (const auto &[name, value] : iter->second.asObject()) {
      this->capabilities.features[name] = value.isBool() && value.asBool();
    }
  }

  // Node.js older than the requirement cannot serve as the oracle: the corpus relies on syntax it
  // does not implement. Reject it here as well, so a binary swapped in after configure time is
  // reported instead of silently skipping (or, worse, diverging on) every case.
  const auto required = NodeVersion::required();
  const auto actual = NodeVersion::parse(this->nodeVersion);
  if (!actual.valid) {
    return this->fail(err, "unreadable Node.js version from the oracle banner: " + line);
  }
  if (actual < required) {
    return this->fail(err, "the oracle is Node.js " + this->nodeVersion + ", but at least " +
                               required.toString() + " is required (see NODE_MIN_VERSION)");
  }
  return true;
}

bool Oracle::fail(std::string &err, std::string message) {
  err = std::move(message);
  this->shutdown();
  return false;
}

MatchResult Oracle::match(const Case &c) {
  std::string err;
  auto json = this->request(c, err);
  MatchResult ret;
  if (json.isInvalid() || !json.isObject()) {
    ret.kind = MatchResult::Kind::RUNTIME_ERROR;
    ret.message = err.empty() ? "invalid response from the oracle" : err;
    return ret;
  }

  const auto &obj = json.asObject();
  auto statusIter = obj.find("status");
  if (statusIter == obj.end() || !statusIter->second.isString()) {
    ret.kind = MatchResult::Kind::RUNTIME_ERROR;
    ret.message = "response without status from the oracle";
    return ret;
  }
  const std::string &status = statusIter->second.asString();
  if (status == "nomatch") {
    ret.kind = MatchResult::Kind::NOMATCH;
    return ret;
  }
  if (status == "syntax") {
    ret.kind = MatchResult::Kind::COMPILE_ERROR;
    if (auto iter = obj.find("message"); iter != obj.end() && iter->second.isString()) {
      ret.message = iter->second.asString();
    }
    return ret;
  }
  if (status == "error") {
    ret.kind = MatchResult::Kind::RUNTIME_ERROR;
    if (auto iter = obj.find("message"); iter != obj.end() && iter->second.isString()) {
      ret.message = iter->second.asString();
    }
    return ret;
  }
  if (status != "match") {
    ret.kind = MatchResult::Kind::RUNTIME_ERROR;
    ret.message = "unknown oracle status: " + status;
    return ret;
  }

  auto capsIter = obj.find("captures");
  if (capsIter == obj.end() || !capsIter->second.isArray()) {
    ret.kind = MatchResult::Kind::RUNTIME_ERROR;
    ret.message = "match response without captures";
    return ret;
  }

  // the oracle reports UTF-16 code unit indices; translate them into UTF-8 byte offsets so the
  // numbers are directly comparable with what arsh reports
  const Utf16Index index = Utf16Index::create(c.input);
  ret.kind = MatchResult::Kind::MATCH;
  for (const auto &e : capsIter->second.asArray()) {
    CaptureRange range;
    if (!e.isNull()) {
      if (!e.isArray() || e.asArray().size() != 2 || !e.asArray()[0].isLong() ||
          !e.asArray()[1].isLong()) {
        ret.kind = MatchResult::Kind::RUNTIME_ERROR;
        ret.message = "malformed capture in the oracle response";
        ret.captures.clear();
        return ret;
      }
      const auto start = index.toByteOffset(static_cast<uint32_t>(e.asArray()[0].asLong()));
      const auto end = index.toByteOffset(static_cast<uint32_t>(e.asArray()[1].asLong()));
      if (start == Utf16Index::INVALID || end == Utf16Index::INVALID) {
        ret.kind = MatchResult::Kind::RUNTIME_ERROR;
        ret.message = "the oracle reported a capture boundary inside a surrogate pair";
        ret.captures.clear();
        return ret;
      }
      range.set = true;
      range.start = start;
      range.end = end;
    }
    ret.captures.push_back(range);
  }
  return ret;
}

json::JSON Oracle::request(const Case &c, std::string &err) {
  const uint64_t id = this->nextId++;
  json::JSON req(json::object(json::JSONMember("id", json::JSON(static_cast<int64_t>(id))),
                              json::JSONMember("pattern", json::JSON(c.pattern)),
                              json::JSONMember("flags", json::JSON(c.flags)),
                              json::JSONMember("input", json::JSON(c.input))));
  std::string line = req.serialize();
  line += '\n';
  if (!this->writeAll(line)) {
    err = std::string("cannot write to the oracle: ") + strerror(errno);
    return json::JSON();
  }

  std::string response;
  if (!this->readLine(response, err)) {
    err = "cannot read from the oracle: " + err;
    return json::JSON();
  }
  auto json = json::JSON::fromString(response.c_str());
  if (json.isObject()) {
    // a stale response from a previous case would silently shift every later comparison
    if (auto iter = json.asObject().find("id");
        iter == json.asObject().end() || !iter->second.isLong() ||
        static_cast<uint64_t>(iter->second.asLong()) != id) {
      err = "unexpected response id from the oracle: " + response;
      return json::JSON();
    }
  }
  return json;
}

} // namespace arsh::difftest
