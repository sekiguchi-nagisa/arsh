/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#include <ydsh/ydsh.h>

#include <vector>
#include <iostream>

namespace ydsh {
namespace directive {

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
    unsigned int status;

public:
    Directive() : result(DS_STATUS_SUCCESS), params(), status(0) {}
    ~Directive() = default;

    unsigned int getResult() const {
        return this->result;
    }

    void setResult(unsigned int status) {
        this->result = status;
    }

    void appendParam(std::string &&param) {
        this->params.push_back(std::move(param));
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

    static bool init(const char *fileName, Directive &d);

    static bool init(const char *sourceName, const char *src, Directive &d);
};

} // namespace directive
} // namespace ydsh


#endif //YDSH_DIRECTIVE_H
