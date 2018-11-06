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

#ifndef YDSH_PROCESS_H
#define YDSH_PROCESS_H

#include <termios.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <initializer_list>
#include <functional>

#include <misc/noncopyable.h>

namespace process {

struct WaitStatus {
    enum Kind : unsigned char {
        ERROR,  // if error happened
        EXITED,
        SIGNALED,
        STOPPED,
    };

    Kind kind;

    int value;

    int toShellStatus() const {
        int status = this->value;
        if(this->value == SIGNALED || this->value == STOPPED) {
            status += 128;
        }
        return status;
    }

    explicit operator bool() const {
        return this->kind != ERROR;
    }

    bool isTerminated() const {
        return this->kind == EXITED || this->kind == SIGNALED;
    }
};

struct Output {
    WaitStatus status;
    std::string out;
    std::string err;
};

class ProcHandle {
private:
    /**
     * after call wait, will be -1
     */
    pid_t pid_;

    WaitStatus status_{WaitStatus::EXITED, 0};

    /**
     * after call wait, will be -1
     */
    int in_;

    /**
     * after call wait, will be -1
     */
    int out_;

    /**
     * after call wait, will be -1
     */
    int err_;

public:
    NON_COPYABLE(ProcHandle);

    ProcHandle() : ProcHandle(-1, -1, -1, -1) {}

    ProcHandle(pid_t pid, int in, int out, int err) noexcept : pid_(pid), in_(in), out_(out), err_(err) {}

    ProcHandle(ProcHandle &&proc) noexcept : pid_(proc.pid_), status_(proc.status_), in_(proc.in_), out_(proc.out_), err_(proc.err_) {
        proc.detach();
    }

    ~ProcHandle();

    ProcHandle &operator=(ProcHandle &&proc) noexcept {
        auto tmp(std::move(proc));
        this->swap(tmp);
        return *this;
    }

    void swap(ProcHandle &proc) noexcept {
        std::swap(this->pid_, proc.pid_);
        std::swap(this->status_, proc.status_);
        std::swap(this->in_, proc.in_);
        std::swap(this->out_, proc.out_);
        std::swap(this->err_, proc.err_);
    }

    pid_t pid() const {
        return this->pid_;
    }

    int in() const {
        return this->in_;
    }

    int out() const {
        return this->out_;
    }

    int err() const {
        return this->err_;
    }

    explicit operator bool() const {
        return this->pid() > -1;
    }

    /**
     * wait process termination
     * @return
     */
    WaitStatus wait();

    pid_t detach() {
        pid_t pid = this->pid_;
        this->pid_ = -1;
        this->in_ = -1;
        this->out_ = -1;
        this->err_ = -1;
        return pid;
    }

    std::pair<std::string, std::string> readAll(int timeout = -1) const;

    Output waitAndGetResult(bool removeLastSpace = true);
};

struct IOConfig {
    enum FDType : int {
        INHERIT = -1,   // inherit parent file descriptor
        PIPE    = -2,   // create pipe
        PTY     = -3,   // create pty
    };

    struct FDWrapper {
        int fd;

        FDWrapper(int fd) : fd(fd) {}   //NOLINT
        FDWrapper(FDType type) : fd(static_cast<int>(type)) {}  //NOLINT

        explicit operator bool() const {
            return this->fd > -1;
        }

        bool is(FDType type) const {
            return this->fd == static_cast<int>(type);
        }
    };

    FDWrapper in;
    FDWrapper out;
    FDWrapper err;

    IOConfig(FDWrapper in, FDWrapper out, FDWrapper err) : in(in), out(out), err(err) {}
    IOConfig() : IOConfig(INHERIT, INHERIT, INHERIT) {}
};

void xcfmakesane(termios &term);


class ProcBuilder {
private:
    IOConfig config;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> env;
    std::string cwd;

    /**
     * if IOConfig has PTY, set terminal setting.
     */
    termios term;

    unsigned short row{24};
    unsigned short col{80};

    /**
     * called before exec process
     */
    std::function<void()> beforeExec;

public:
    explicit ProcBuilder(const char *cmdName) : args{cmdName} {
        cfmakeraw(&this->term);
    }

    ProcBuilder(std::initializer_list<const char *> list) {
        for(auto &v : list) {
            this->args.emplace_back(v);
        }
        cfmakeraw(&this->term);
    }

    ~ProcBuilder() = default;

    template <typename T>
    ProcBuilder &addArg(T && arg) {
        this->args.emplace_back(std::forward<T>(arg));
        return *this;
    }

    ProcBuilder &addArgs(const std::vector<std::string> &values);

    ProcBuilder &addEnv(const char *name, const char *value) {
        this->env.emplace(name, value);
        return *this;
    }

    /**
     * close old fd and set
     * @param fd
     * @return
     */
    ProcBuilder &setIn(IOConfig::FDWrapper fd) {
        this->config.in = fd;
        return *this;
    }

    ProcBuilder &setOut(IOConfig::FDWrapper fd) {
        this->config.out = fd;
        return *this;
    }

    ProcBuilder &setErr(IOConfig::FDWrapper fd) {
        this->config.err = fd;
        return *this;
    }

    ProcBuilder &setWorkingDir(const char *dir) {
        this->cwd = dir;
        return *this;
    }

    ProcBuilder &setTerm(termios &term) {
        this->term = term;
        return *this;
    }

    ProcBuilder &setWinSize(unsigned short row, unsigned short col) {
        this->row = row;
        this->col = col;
        return *this;
    }

    ProcBuilder &setBeforeExec(const std::function<void()> &func) {
        this->beforeExec = func;
        return *this;
    }

    ProcHandle operator()();

    /**
     * if old out/err is INHERIT, set PIPE.
     * @param removeLastSpace
     * @return
     */
    Output execAndGetResult(bool removeLastSpace = true) {
        if(this->config.out.fd == IOConfig::INHERIT) {
            this->config.out = IOConfig::PIPE;
        }
        if(this->config.err.fd == IOConfig::INHERIT) {
            this->config.err = IOConfig::PIPE;
        }
        return (*this)().waitAndGetResult(removeLastSpace);
    }

    WaitStatus exec() {
        return (*this)().wait();
    }

    template <typename Func>
    static ProcHandle spawn(IOConfig config, Func func) {
        ProcHandle handle = spawnImpl(config);
        if(handle) {
            return handle;
        } else {
#ifdef __EXCEPTIONS
            try {
#endif
                int ret = func();
                exit(ret);

#ifdef __EXCEPTIONS
            } catch(...) {
                abort();
            }
#endif
        }
    }

private:
    static ProcHandle spawnImpl(IOConfig config);

    void syncPWD() const;

    void syncEnv() const;

    int findPTY() const;
};

} // namespace process

#endif //YDSH_PROCESS_H
