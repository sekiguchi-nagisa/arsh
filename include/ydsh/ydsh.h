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

#ifndef YDSH_YDSH_H
#define YDSH_YDSH_H

#include <vector>
#include <memory>

namespace ydsh {

enum class ExecStatus : unsigned int {
    SUCCESS,
    PARSE_ERROR,
    TYPE_ERROR,
    RUNTIME_ERROR,
    ASSERTION_ERROR,
    EXIT,
};

struct ExecutionEngine {
    virtual ~ExecutionEngine() = default;

    ExecStatus eval(const char *line) { return this->eval(line, false); }
    virtual ExecStatus eval(const char *line, bool zeroCopy) = 0;
    virtual ExecStatus eval(const char *sourceName, FILE *fp) = 0;
    virtual void setLineNum(unsigned int lineNum) = 0;
    virtual unsigned int getLineNum() = 0;
    virtual void setArguments(const std::vector<const char *> &args) = 0;
    virtual void setDumpUntypedAST(bool dump) = 0;
    virtual void setDumpTypedAST(bool dump) = 0;
    virtual void setParseOnly(bool parseOnly) = 0;
    virtual void setAssertion(bool assertion) = 0;
    virtual void setToplevelprinting(bool print) = 0;
    virtual const std::string &getWorkingDir() = 0;

    /**
     * get exit status of recently executed command.(also exit command)
     */
    virtual int getExitStatus() = 0;

    static std::unique_ptr<ExecutionEngine> createInstance();
};

} // namespace ydsh

#endif //YDSH_YDSH_H
