/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#ifndef YDSH_TEST_COMMON_HPP
#define YDSH_TEST_COMMON_HPP

#include <ostream>
#include <vector>
#include <string>
#include <initializer_list>

#include <misc/buffer.hpp>

// common utility for test

class TempFileFactory {
protected:
    std::string tmpFileName;

    TempFileFactory() = default;
    virtual ~TempFileFactory() = default;

    void createTemp();

    void deleteTemp();

public:
    const std::string &getTmpFileName() const {
        return this->tmpFileName;
    }
};

struct Output {
    ydsh::ByteBuffer out;
    ydsh::ByteBuffer err;
};

struct CmdResult {
    int status;
    std::string out;
    std::string err;
};

class CommandBuilder {
private:
    std::vector<std::string> args;

public:
    CommandBuilder(const char *cmdName) : args{cmdName} {}

    CommandBuilder(std::initializer_list<const char *> list) {
        for(auto &v : list) {
            this->args.emplace_back(v);
        }
    }

    ~CommandBuilder() = default;

    CommandBuilder &addArg(const char *arg) {
        this->args.emplace_back(arg);
        return *this;
    }

    CommandBuilder &addArg(const std::string &str) {
        return this->addArg(str.c_str());
    }

    CommandBuilder &addArgs(const std::vector<std::string> &values);

    std::string execAndGetOutput(bool removeLastSpace = true) const {
        auto r = this->execAndGetResult(removeLastSpace);
        return std::move(r.out);
    }

    CmdResult execAndGetResult(bool removeLastSpace = true) const;

    int exec(Output *output = nullptr) const;

    int exec(Output &output) const {
        return this->exec(&output);
    }

private:
    void syncPWD() const;
};

inline bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::ostream &operator<<(std::ostream &stream, const ydsh::ByteBuffer &buffer);

#define ASSERT_(F) do { SCOPED_TRACE(""); F; } while(false)

#endif //YDSH_TEST_COMMON_HPP
