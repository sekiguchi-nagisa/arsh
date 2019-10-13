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

#ifndef YDSH_TEST_COMMON_H
#define YDSH_TEST_COMMON_H

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <process.h>
#include <ansi.h>

#include "misc/resource.hpp"

using namespace process;

// common utility for test

class TempFileFactory {
protected:
    std::string tmpDirName;
    std::string tmpFileName;

public:
    TempFileFactory();

    virtual ~TempFileFactory();

    const char *getTempDirName() const {
        return this->tmpDirName.c_str();
    }

    const char *getTempFileName() const {
        return this->tmpFileName.c_str();
    }

    /**
     * create temp file with content
     * @param name
     * if empty string, generate random name. after file creation, write full path to it.
     * @param content
     * @return
     * opened file ptr with 'w+b' mode.
     */
    ydsh::FilePtr createTempFilePtr(std::string &name, const std::string &content) const;

    /**
     * create temp file with content
     * @param baseName
     * if null or empty string, generate random name.
     * @param content
     * @return
     * full path of temp file
     */
    std::string createTempFile(const char *baseName, const std::string &content) const {
        std::string name = baseName != nullptr ? baseName : "";
        this->createTempFilePtr(name, content);
        return name;
    }
};

std::string format(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));

inline std::string makeLineMarker(const std::string &line) {
    std::string str = "^";
    for(unsigned int i = 1; i < line.size(); i++) {
        str += "~";
    }
    return str;
}

class Extractor {
private:
    const char *str;

public:
    explicit Extractor(const char *str) : str(str) {}

    template <typename ...Arg>
    int operator()(Arg &&... arg) {
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

    template <typename F, typename ...T>
    int delegate(F &&first, T &&...rest) {
        int ret = this->extract(std::forward<F>(first));
        return ret == 0 ? this->delegate(std::forward<T>(rest)...) : ret;
    }
};


struct ExpectOutput : public ::testing::Test {
    void expect(const Output &output, int status = 0,
                WaitStatus::Kind type = WaitStatus::EXITED,
                const std::string &out = "", const std::string &err = "") {
        EXPECT_EQ(out, output.out);
        EXPECT_EQ(err, output.err);
        EXPECT_EQ(status, output.status.value);
        EXPECT_EQ(type, output.status.kind);
        ASSERT_FALSE(this->HasFailure());
    }

    void expect(ProcBuilder &&builder, int status, const std::string &out = "", const std::string &err = "") {
        auto result = builder.execAndGetResult(false);
        ASSERT_NO_FATAL_FAILURE(this->expect(result, status, WaitStatus::EXITED, out, err));
    }

    void expectRegex(const Output &output, int status, WaitStatus::Kind type,
                     const std::string &out, const std::string &err = "") {
        EXPECT_EQ(status, output.status.value);
        EXPECT_EQ(type, output.status.kind);
        EXPECT_THAT(output.out, ::testing::MatchesRegex(out));
        EXPECT_THAT(output.err, ::testing::MatchesRegex(err));
        ASSERT_FALSE(this->HasFailure());
    }

    void expectRegex(ProcBuilder &&builder, int status, const std::string &out, const std::string &err = "") {
        auto result = builder.execAndGetResult(false);
        ASSERT_NO_FATAL_FAILURE(this->expectRegex(result, status, WaitStatus::EXITED, out, err));
    }
};

class InteractiveBase : public ExpectOutput {
protected:
    int timeout{80};

    ProcHandle handle;

    const std::string binPath;

    const std::string workingDir;

    const bool ttyEmulation;

    InteractiveBase(const char *binPath, const char *dir, bool ttyEmulation = true) :
                    binPath(binPath), workingDir(dir), ttyEmulation(ttyEmulation) {}

    template <typename ... T>
    void invoke(T && ...args) {
        std::vector<std::string> values = { std::forward<T>(args)... };
        this->invokeImpl(values);
    }

    void invokeImpl(const std::vector<std::string> &args);

    void send(const char *str) {
        int r = write(this->handle.in(), str, strlen(str));
        (void) r;
        fsync(this->handle.in());
    }

    void interpret(std::string &line);

    std::pair<std::string, std::string> readAll() {
        auto ret = this->handle.readAll(this->timeout);
        if(this->ttyEmulation) {
            this->interpret(ret.first);
        }
        return ret;
    }

    void expectRegex(const char *out = "", const char *err = "") {
        ASSERT_TRUE(out != nullptr);
        ASSERT_TRUE(err != nullptr);

        auto pair = this->readAll();
        EXPECT_THAT(pair.first, ::testing::MatchesRegex(out));
        EXPECT_THAT(pair.second, ::testing::MatchesRegex(err));
        ASSERT_FALSE(this->HasFailure());
    }

    void expect(const char *out = "", const char *err = "") {
        ASSERT_TRUE(out != nullptr);
        ASSERT_TRUE(err != nullptr);

        auto pair = this->readAll();
        EXPECT_EQ(out, pair.first);
        EXPECT_EQ(err, pair.second);
        ASSERT_FALSE(this->HasFailure());
    }

    void sendAndExpect(const char *str, const char *out = "", const char *err = "") {
        std::string eout = str;
        this->send(str);

        if(this->ttyEmulation) {
            this->send("\r");
            eout += "\n";
            eout += out;
        }
        ASSERT_NO_FATAL_FAILURE(this->expect(eout.c_str(), err));
    }

    void waitAndExpect(int status = 0, WaitStatus::Kind type = WaitStatus::EXITED,
                       const char *out = "", const char *err = "") {
        auto pair = this->readAll();
        auto s = this->handle.wait();
        auto ret = Output {
            .status = s,
            .out = std::move(pair.first),
            .err = std::move(pair.second)
        };
        ASSERT_NO_FATAL_FAILURE(ExpectOutput::expect(ret, status, type, out, err));
    }
};


#endif //YDSH_TEST_COMMON_H
