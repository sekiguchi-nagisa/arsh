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

#ifndef YDSH_TEST_COMMON_H
#define YDSH_TEST_COMMON_H

#include <unistd.h>

#include <ostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <initializer_list>

#include <misc/buffer.hpp>

// common utility for test

#define ASSERT_(F) do { SCOPED_TRACE(""); F; } while(false)

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

struct Output {
    ydsh::ByteBuffer out;
    ydsh::ByteBuffer err;
};

struct ProcResult {
    int status;
    std::string out;
    std::string err;
};

class Proc {
private:
    /**
     * after call wait, will be -1
     */
    pid_t pid_;

    /**
     * after call wait, will be -1
     */
    int out_;

    /**
     * after call wait, will be -1
     */
    int err_;

public:
    NON_COPYABLE(Proc);

    Proc() : Proc(-1, -1, -1) {}

    Proc(pid_t pid, int out, int err) noexcept : pid_(pid), out_(out), err_(err) {}

    Proc(Proc &&proc) noexcept : pid_(proc.pid_), out_(proc.out_), err_(proc.err_) {
        proc.pid_ = -1;
        proc.out_ = -1;
        proc.err_ = -1;
    }

    ~Proc() {
        this->wait();
    }

    Proc &operator=(Proc &&proc) noexcept {
        auto tmp(std::move(proc));
        this->swap(tmp);
        return *this;
    }

    void swap(Proc &proc) noexcept {
        std::swap(this->pid_, proc.pid_);
        std::swap(this->out_, proc.out_);
        std::swap(this->err_, proc.err_);
    }

    pid_t pid() const {
        return this->pid_;
    }

    int out() const {
        return this->out_;
    }

    int err() const {
        return this->err_;
    }

    operator bool() const {
        return this->pid() > -1;
    }

    int wait();

    Output readAll();

    ProcResult waitAndGetResult(bool removeLastSpace = true);
};

class ProcBuilder {
private:
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> env;

public:
    ProcBuilder(const char *cmdName) : args{cmdName} {}

    ProcBuilder(std::initializer_list<const char *> list) {
        for(auto &v : list) {
            this->args.emplace_back(v);
        }
    }

    ~ProcBuilder() = default;

    ProcBuilder &addArg(const char *arg) {
        this->args.emplace_back(arg);
        return *this;
    }

    ProcBuilder &addArg(const std::string &str) {
        return this->addArg(str.c_str());
    }

    ProcBuilder &addArgs(const std::vector<std::string> &values);

    ProcBuilder &addEnv(const char *name, const char *value) {
        this->env.insert({name, value});
        return *this;
    }

    Proc spawn(bool usePipe = false) const;

    std::string execAndGetOutput(bool removeLastSpace = true) const {
        auto r = this->execAndGetResult(removeLastSpace);
        return std::move(r.out);
    }

    ProcResult execAndGetResult(bool removeLastSpace = true) const {
        return this->spawn(true).waitAndGetResult(removeLastSpace);
    }

    int exec(Output *output = nullptr) const;

    int exec(Output &output) const {
        return this->exec(&output);
    }

    template <typename Func>
    static Proc fork(bool usePipe, Func func) {
        Proc proc = forkImpl(usePipe);
        if(proc) {
            return proc;
        } else {
            int ret = func();
            exit(ret);
        }
    }

private:
    static Proc forkImpl(bool usePipe);

    void syncPWD() const;

    void syncEnv() const;
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

#endif //YDSH_TEST_COMMON_H
