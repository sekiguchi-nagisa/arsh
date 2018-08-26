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

#include <process.h>

using namespace process;

// common utility for test

class TempFileFactory {
protected:
    char *tmpDirName{nullptr};
    char *tmpFileName{nullptr};

    virtual ~TempFileFactory();

    void createTemp();

    void deleteTemp();

public:
    const char *getTmpDirName() const {
        return this->tmpDirName;
    }

    const char *getTmpFileName() const {
        return this->tmpFileName;
    }

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
                const char *out = "", const char *err = "") {
        SCOPED_TRACE("");

        if(out != nullptr) {
            ASSERT_EQ(out, output.out);
        }
        if(err != nullptr) {
            ASSERT_EQ(err, output.err);
        }
        ASSERT_EQ(status, output.status.value);
        ASSERT_EQ(type, output.status.kind);
    }
};


#endif //YDSH_TEST_COMMON_H
