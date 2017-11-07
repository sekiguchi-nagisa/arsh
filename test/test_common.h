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

#include <misc/noncopyable.h>

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
    int status;
    std::string out;
    std::string err;
};

class ProcHandle {
private:
    /**
     * after call wait, will be -1
     */
    pid_t pid_;

    int status_{0};

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

    ProcHandle(ProcHandle &&proc) noexcept : pid_(proc.pid_), in_(proc.in_), out_(proc.out_), err_(proc.err_) {
        proc.pid_ = -1;
        proc.in_ = -1;
        proc.out_ = -1;
        proc.err_ = -1;
    }

    ~ProcHandle() {
        this->wait();
    }

    ProcHandle &operator=(ProcHandle &&proc) noexcept {
        auto tmp(std::move(proc));
        this->swap(tmp);
        return *this;
    }

    void swap(ProcHandle &proc) noexcept {
        std::swap(this->pid_, proc.pid_);
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

    operator bool() const {
        return this->pid() > -1;
    }

    /**
     * wait process termination
     * @return
     * raw exit status
     */
    int wait();

    std::pair<std::string, std::string> readAll();

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

        FDWrapper(int fd) : fd(fd) {}
        FDWrapper(FDType type) : fd(static_cast<int>(type)) {}

        operator bool() const {
            return this->fd > -1;
        }
    };

    FDWrapper in;
    FDWrapper out;
    FDWrapper err;

    IOConfig(FDWrapper in, FDWrapper out, FDWrapper err) : in(in), out(out), err(err) {}
    IOConfig() : IOConfig(INHERIT, INHERIT, INHERIT) {}
};

class ProcBuilder {
private:
    IOConfig config{};
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> env;
    std::string cwd;

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

    int exec() {
        return (*this)().wait();
    }

    template <typename Func>
    static ProcHandle spawn(IOConfig config, Func func) {
        ProcHandle handle = spawnImpl(config);
        if(handle) {
            return handle;
        } else {
            int ret = func();
            exit(ret);
        }
    }

private:
    static ProcHandle spawnImpl(IOConfig config);

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
