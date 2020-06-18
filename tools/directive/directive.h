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

#ifndef YDSH_TOOLS_DIRECTIVE_H
#define YDSH_TOOLS_DIRECTIVE_H

#include <vector>
#include <iostream>
#include <memory>
#include <cstring>
#include <unordered_map>

#include <ydsh/ydsh.h>

namespace ydsh {
namespace directive {

class Directive {
private:
    /**
     * kind of status.(DS_STATUS_*)
     */
    unsigned int result{DS_ERROR_KIND_SUCCESS};

    std::vector<std::string> params;

    /**
     * for command exit status
     */
    unsigned int status{0};

    unsigned int lineNum{0};

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
    char *out{nullptr};

    /**
     * indicate stderr value
     */
    char *err{nullptr};

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
    ~Directive();

    unsigned int getResult() const {
        return this->result;
    }

    void setResult(unsigned int v) {
        this->result = v;
    }

    void appendParam(const std::string &param) {
        this->params.push_back(param);
    }

    const std::vector<std::string> &getParams() const {
        return this->params;
    }

    void setStatus(unsigned int s) {
        this->status = s;
    }

    unsigned int getStatus() const {
        return this->status;
    }

    void setLineNum(unsigned int v) {
        this->lineNum = v;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    void setErrorKind(const std::string &kind) {
        this->errorKind = kind;
    }

    const std::string &getErrorKind() const {
        return this->errorKind;
    }

    void setIn(const std::string &str) {
        this->in = str;
    }

    const std::string &getIn() const {
        return this->in;
    }

    void setOut(const std::string &str) {
        this->out = strdup(str.c_str());
    }

    const char *getOut() const {
        return this->out;
    }

    void setErr(const std::string &str) {
        this->err = strdup(str.c_str());
    }

    const char *getErr() const {
        return this->err;
    }

    void setFileName(const char *name) {
        this->fileName = name;
    }

    const std::string &getFileName() const {
        return this->fileName;
    }

    void addEnv(const std::string &name, const std::string &value) {
        this->envs[name] = value;
    }

    const std::unordered_map<std::string, std::string> &getEnvs() const {
        return this->envs;
    }

    void setIgnoredPlatform(bool ignore) {
        this->ignoredPlatform = ignore;
    }

    bool isIgnoredPlatform() const {
        return this->ignoredPlatform;
    }

    static bool init(const char *fileName, Directive &d);

    static bool init(const char *sourceName, const char *src, Directive &d);
};

} // namespace directive
} // namespace ydsh


#endif //YDSH_TOOLS_DIRECTIVE_H
