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

using namespace process;

// common utility for test

class TempFileFactory {
protected:
    char *tmpDirName{nullptr};
    char *tmpFileName{nullptr};

private:
    void createTemp();
    void deleteTemp();

public:
    TempFileFactory();

    virtual ~TempFileFactory();

    const char *getTempDirName() const {
        return this->tmpDirName;
    }

    const char *getTempFileName() const {
        return this->tmpFileName;
    }

    std::string createTempFile(const char *baseName, const std::string &content) const;

private:
    void freeName();
};

std::string format(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));

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


#define ASSERT_(F) do { SCOPED_TRACE(""); F; } while(false)

struct ExpectOutput : public ::testing::Test {
    void expect(const Output &output, int status = 0,
                WaitStatus::Kind type = WaitStatus::EXITED,
                const std::string &out = "", const std::string &err = "") {
        SCOPED_TRACE("");

        ASSERT_EQ(out, output.out);
        ASSERT_EQ(err, output.err);
        ASSERT_EQ(status, output.status.value);
        ASSERT_EQ(type, output.status.kind);
    }

    void expect(ProcBuilder &&builder, int status, const std::string &out = "", const std::string &err = "") {
        SCOPED_TRACE("");

        auto result = builder.execAndGetResult(false);
        this->expect(result, status, WaitStatus::EXITED, out, err);
    }

    void expectRegex(const Output &output, int status, WaitStatus::Kind type,
                     const std::string &out, const std::string &err = "") {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(status, output.status.value));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(type, output.status.kind));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(output.out, ::testing::MatchesRegex(out)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(output.err, ::testing::MatchesRegex(err)));
    }

    void expectRegex(ProcBuilder &&builder, int status, const std::string &out, const std::string &err = "") {
        auto result = builder.execAndGetResult(false);
        this->expectRegex(result, status, WaitStatus::EXITED, out, err);
    }
};

class InteractiveBase : public ExpectOutput {
private:
    ProcHandle handle;

    const std::string binPath;

    const std::string workingDir;

protected:
    explicit InteractiveBase(const char *binPath, const char *dir) : binPath(binPath), workingDir(dir) {}

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
        auto ret = this->handle.readAll(80);
        this->interpret(ret.first);
        return ret;
    }

    void expectRegex(const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto pair = this->readAll();
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(pair.first, ::testing::MatchesRegex(out)));
        ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(pair.second, ::testing::MatchesRegex(err)));
    }

    void expect(const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(out != nullptr));
        ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(err != nullptr));

        auto pair = this->readAll();
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(out, pair.first));
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(err, pair.second));
    }

    void sendAndExpect(const char *str, const char *out = "", const char *err = "") {
        this->send(str);
        this->send("\r");

        std::string eout = str;
        eout += "\n";
        eout += out;
        this->expect(eout.c_str(), err);
    }

    void waitAndExpect(int status = 0, WaitStatus::Kind type = WaitStatus::EXITED,
                       const char *out = "", const char *err = "") {
        auto ret = this->handle.waitAndGetResult(false);
        this->interpret(ret.out);
        ExpectOutput::expect(ret, status, type, out, err);
    }
};


#endif //YDSH_TEST_COMMON_H
