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

#ifndef YDSH_DIRECTIVE_H
#define YDSH_DIRECTIVE_H

#include <vector>
#include <iostream>
#include <memory>

#include <ydsh/ydsh.h>

namespace ydsh {
namespace directive {

enum class RunCondition : unsigned int {
    IGNORE,
    TRUE,
    FALSE,
};

class Directive {
private:
    /**
     * kind of status.(DS_STATUS_*)
     */
    unsigned int result;

    std::vector<std::string> params;

    /**
     * for command exit status
     */
    unsigned int status{0};

    unsigned int lineNum{0};

    /**
     * default is IGNORE
     */
    RunCondition ifHaveDBus{RunCondition::IGNORE};

    /**
     * represent parse or type error name or raised exception type name.
     * default is empty string
     */
    std::string errorKind;

public:
    Directive() : result(DS_ERROR_KIND_SUCCESS) {}

    ~Directive() = default;

    unsigned int getResult() const {
        return this->result;
    }

    void setResult(unsigned int status) {
        this->result = status;
    }

    void appendParam(const std::string &param) {
        this->params.push_back(param);
    }

    const std::vector<std::string> &getParams() const {
        return this->params;
    }

    void setStatus(unsigned int status) {
        this->status = status;
    }

    unsigned int getStatus() const {
        return this->status;
    }

    void setLineNum(unsigned int lineNum) {
        this->lineNum = lineNum;
    }

    unsigned int getLineNum() const {
        return this->lineNum;
    }

    void setIfHaveDBus(bool value) {
        if(value) {
            this->ifHaveDBus = RunCondition::TRUE;
        } else {
            this->ifHaveDBus = RunCondition::FALSE;
        }
    }

    RunCondition getIfHaveDBus() const {
        return this->ifHaveDBus;
    }

    void setErrorKind(const std::string &kind) {
        this->errorKind = kind;
    }

    const std::string &getErrorKind() const {
        return this->errorKind;
    }

    static bool init(const char *fileName, Directive &d);

    static bool init(const char *sourceName, const char *src, Directive &d);
};

} // namespace directive
} // namespace ydsh


#endif //YDSH_DIRECTIVE_H
