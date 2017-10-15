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

#include <unistd.h>

#include <ostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <initializer_list>

#include <misc/buffer.hpp>

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

struct Output {
    ydsh::ByteBuffer out;
    ydsh::ByteBuffer err;
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
    int fd_out_;

    /**
     * after call wait, will be -1
     */
    int fd_err_;

public:
    NON_COPYABLE(Proc);

    Proc() : Proc(-1, -1, -1) {}

    Proc(pid_t pid, int out, int err) noexcept : pid_(pid), fd_out_(out), fd_err_(err) {}

    Proc(Proc &&proc) noexcept : pid_(proc.pid_), fd_out_(proc.fd_out_), fd_err_(proc.fd_err_) {
        proc.pid_ = -1;
        proc.fd_out_ = -1;
        proc.fd_err_ = -1;
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
        std::swap(this->fd_out_, proc.fd_out_);
        std::swap(this->fd_err_, proc.fd_err_);
    }

    pid_t pid() const {
        return this->pid_;
    }

    int fd_out() const {
        return this->fd_out_;
    }

    int fd_err() const {
        return this->fd_err_;
    }

    operator bool() const {
        return this->pid() > -1;
    }

    int wait();

    Output readAll();
};

struct CmdResult {
    int status;
    std::string out;
    std::string err;
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

    CmdResult execAndGetResult(bool removeLastSpace = true) const;

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

inline bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::ostream &operator<<(std::ostream &stream, const ydsh::ByteBuffer &buffer);

std::string format(const char *fmt, ...) __attribute__ ((format(printf, 1, 2)));

#define ASSERT_(F) do { SCOPED_TRACE(""); F; } while(false)

#endif //YDSH_TEST_COMMON_HPP
