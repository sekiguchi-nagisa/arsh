/*
 * Copyright (C) 2015-2017 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_DIRECTIVE_DIRECTIVE_H
#define YDSH_TOOLS_DIRECTIVE_DIRECTIVE_H

#include <cstring>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include <misc/resource.hpp>

#include <ydsh/ydsh.h>

namespace ydsh::directive {

class Directive {
private:
  /**
   * kind of status.(DS_ERROR_KIND_*)
   *
   * if -1, invalid DS_ERROR_KIND
   */
  int result{DS_ERROR_KIND_SUCCESS};

  /**
   * for command exit status
   */
  int status{0};

  unsigned int lineNum{0};

  unsigned int chars{0};

  std::vector<std::string> params;

  /**
   * represent parse or type error name or raised exception type name.
   * default is empty string
   */
  std::string errorKind;

  /**
   * indicate stdin value. the size must be under PIPE_BUF.
   * if not specified stdin value, is empty string
   */
  std::string in;

  /**
   * indicate stdout value
   */
  CStrPtr out;

  /**
   * indicate stderr value
   */
  CStrPtr err;

  /**
   * indicate error file name.
   * if empty, file name is not specified.
   */
  std::string fileName;

  std::unordered_map<std::string, std::string> envs;

  /**
   * if true, suppress execution.
   */
  bool ignoredPlatform{false};

public:
  int getKind() const { return this->result; }

  void setKind(int v) { this->result = v; }

  void appendParam(const std::string &param) { this->params.push_back(param); }

  const std::vector<std::string> &getParams() const { return this->params; }

  void setStatus(int s) { this->status = s; }

  int getStatus() const { return this->status; }

  void setLineNum(unsigned int v) { this->lineNum = v; }

  unsigned int getLineNum() const { return this->lineNum; }

  void setChars(unsigned int v) { this->chars = v; }

  unsigned int getChars() const { return this->chars; }

  void setErrorKind(const std::string &kind) { this->errorKind = kind; }

  const std::string &getErrorKind() const { return this->errorKind; }

  void setIn(const std::string &str) { this->in = str; }

  const std::string &getIn() const { return this->in; }

  void setOut(const std::string &str) { this->out.reset(strdup(str.c_str())); }

  const char *getOut() const { return this->out.get(); }

  void setErr(const std::string &str) { this->err.reset(strdup(str.c_str())); }

  const char *getErr() const { return this->err.get(); }

  void setFileName(const char *name) { this->fileName = name; }

  const std::string &getFileName() const { return this->fileName; }

  void addEnv(const std::string &name, const std::string &value) { this->envs[name] = value; }

  const std::unordered_map<std::string, std::string> &getEnvs() const { return this->envs; }

  void setIgnoredPlatform(bool ignore) { this->ignoredPlatform = ignore; }

  bool isIgnoredPlatform() const { return this->ignoredPlatform; }

  static bool init(const char *fileName, Directive &d);

  static bool init(const char *sourceName, const char *src, Directive &d);
};

/**
 * if cannot resolve, return empty string
 * @param kind
 * @return
 */
const char *toString(DSErrorKind kind);

} // namespace ydsh::directive

#endif // YDSH_TOOLS_DIRECTIVE_DIRECTIVE_H
