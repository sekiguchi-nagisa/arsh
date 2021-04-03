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

#ifndef YDSH_JOB_H
#define YDSH_JOB_H

#include <unistd.h>
#include <fcntl.h>

#include <vector>
#include <type_traits>

#include "misc/resource.hpp"
#include "object.h"

struct DSState;

namespace ydsh {

inline int beForeground(pid_t pid) {
    errno = 0;
    int ttyFd = open("/dev/tty", O_RDONLY);
    int r =  tcsetpgrp(ttyFd, getpgid(pid));
    int old = errno;
    close(ttyFd);
    errno = old;
    return r;
}

class Proc {
public:
    enum State : unsigned char {
        RUNNING,
        STOPPED,    // stopped by SIGSTOP or SIGTSTP
        TERMINATED, // already called waitpid
    };

#define EACH_WAIT_OP(OP) \
    OP(BLOCKING) \
    OP(BLOCK_UNTRACED) \
    OP(NONBLOCKING)

    enum WaitOp : unsigned char {
#define GEN_ENUM(OP) OP,
        EACH_WAIT_OP(GEN_ENUM)
#undef GEN_ENUM
    };

private:
    pid_t pid_;
    State state_;

    /**
     * enabled when `state' is TERMINATED.
     */
    unsigned char exitStatus_;

    explicit Proc(pid_t pid) : pid_(pid), state_(RUNNING), exitStatus_(0) {}

public:
    Proc() = default;

    pid_t pid() const {
        return this->pid_;
    }

    State state() const {
        return this->state_;
    }

    int exitStatus() const {
        return this->exitStatus_;
    }

    /**
     * wait for termination
     * @param op
     * @param showSignal
     * if true, print signal message when terminated by signal.
     * @return
     * if waitpid return 0, set 'exitStatus_' and return it.
     * if waitpid return -1, return -1.
     */
    int wait(WaitOp op, bool showSignal = true);

    /**
     * send signal to proc
     * @param sigNum
     * @return
     * if success, return 0.
     */
    int send(int sigNum) const;

    /**
     * after fork, reset signal setting in child process.
     * if Proc#pid() is -1, fork failed due to EAGAIN.
     * @param st
     * @param pgid
     * @param foreground
     * @return
     */
    static Proc fork(DSState &st, pid_t pgid, bool foreground);
};

class JobTable;

class JobObject : public ObjectWithRtti<ObjectKind::Job> {
public:
    static_assert(std::is_pod<Proc>::value, "failed");

    enum class State : unsigned char {
        RUNNING,
        TERMINATED,     // already terminated
        UNCONTROLLED,   // job is not created its own parent process
    };

private:
    /**
     * writable file descriptor (connected to STDIN of Job). must be UnixFD_Object
     */
    ObjPtr<UnixFdObject> inObj;

    /**
     * readable file descriptor (connected to STDOUT of Job). must be UnixFD_Object
     */
    ObjPtr<UnixFdObject> outObj;

    /**
     * after detach, will be 0
     */
    unsigned int jobID{0};

    /**
     * if already closed, will be -1.
     */
    int oldStdin{-1};

    State state{State::RUNNING};

    unsigned short procSize;

    /**
     * initial size is procSize
     */
    Proc procs[];

    friend class JobTable;

    NON_COPYABLE(JobObject);

    JobObject(unsigned int size, const Proc *procs, bool saveStdin,
              ObjPtr<UnixFdObject> inObj, ObjPtr<UnixFdObject> outObj) :
            ObjectWithRtti(TYPE::Job),
            inObj(std::move(inObj)), outObj(std::move(outObj)), procSize(size) {
        for(unsigned int i = 0; i < this->procSize; i++) {
            this->procs[i] = procs[i];
        }
        if(saveStdin) {
            this->oldStdin = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0);
        }
    }

public:
    static ObjPtr<JobObject> create(unsigned int size, const Proc *procs, bool saveStdin,
                                    ObjPtr<UnixFdObject> inObj, ObjPtr<UnixFdObject> outObj) {
        void *ptr = malloc(sizeof(JobObject) + sizeof(Proc) * size);
        auto *entry = new(ptr) JobObject(size, procs, saveStdin, std::move(inObj), std::move(outObj));
        return ObjPtr<JobObject>(entry);
    }

    static ObjPtr<JobObject> create(Proc proc, ObjPtr<UnixFdObject> inObj, ObjPtr<UnixFdObject> outObj) {
        Proc procs[1] = {proc};
        return create(1, procs, false, std::move(inObj), std::move(outObj));
    }

    static void operator delete(void *ptr) noexcept {   //NOLINT
        free(ptr);
    }

    unsigned short getProcSize() const {
        return this->procSize;
    }

    bool available() const {
        return this->state == State::RUNNING;
    }

    const Proc *getProcs() const {
        return this->procs;
    }

    /**
     *
     * @param index
     * @return
     * after termination, return -1.
     */
    pid_t getPid(unsigned int index) const {
        return this->procs[index].pid();
    }

    /**
     *
     * @return
     * after detached, return 0.
     */
    unsigned int getJobID() const {
        return this->jobID;
    }

    bool isControlled() const {
        return this->state != State::UNCONTROLLED;
    }

    DSValue getInObj() const {
        return this->inObj;
    }

    DSValue getOutObj() const {
        return this->outObj;
    }

    /**
     * restore STDIN_FD
     * if has no ownership, do nothing.
     * @return
     * if restore fd, return true.
     * if already called, return false
     */
    bool restoreStdin();

    /**
     * send signal to all processes.
     * if jos is process group leader, send signal to process group
     * @param sigNum
     */
    void send(int sigNum) const;

    /**
     * wait for termination.
     * after termination, `state' will be TERMINATED.
     * @param op
     * @return
     * exit status of last process.
     * if cannot terminate (has no-ownership or has error), return -1 and set errno
     */
    int wait(Proc::WaitOp op);

    bool poll() {
        this->wait(Proc::NONBLOCKING);
        return this->available();
    }
};

using Job = ObjPtr<JobObject>;

class JobTable {    //FIXME: send signal to managed jobs
private:
    std::vector<Job> entries;

    /**
     * if maintain disowned job, `jobSize' is not equivalent to `entries' size.
     */
    unsigned int jobSize{0};

    /**
     * latest attached entry.
     */
    Job latestEntry;

public:
    NON_COPYABLE(JobTable);

    using EntryIter = std::vector<Job>::iterator;
    using ConstEntryIter = std::vector<Job>::const_iterator;

    JobTable() = default;
    ~JobTable() = default;

    void attach(Job job, bool disowned = false);

    /**
     * remove job from JobTable
     * @param jobId
     * if 0, do nothing.
     * @param remove
     * @return
     * detached job.
     * if specified job is not found, return null
     */
    Job detach(unsigned int jobId, bool remove = false);

    /**
     * if has ownership, wait termination.
     * @param entry
     * @param jobctrl
     * @return
     * exit status of last process.
     * after waiting termination, remove entry.
     */
    int waitAndDetach(Job &entry, bool jobctrl) {
        int ret = entry->wait(jobctrl ? Proc::BLOCK_UNTRACED : Proc::BLOCKING);
        if(!entry->available()) {
            this->detach(entry->getJobID(), true);
        }
        return ret;
    }

    void detachAll() {
        for(auto &e : this->entries) {
            e->jobID = 0;
            e->state = JobObject::State::UNCONTROLLED;
        }
        this->entries.clear();
        this->latestEntry.reset();
    }

    /**
     * update status of managed jobs.
     * when a job is terminated, detach job.
     * should call after wait termination of foreground job.
     */
    void updateStatus();

    const Job &getLatestEntry() {
        return this->latestEntry;
    }

    /**
     *
     * @param jobId
     * @return
     * if not found, return nullptr
     */
    Job findEntry(unsigned int jobId) const {
        auto iter = this->findEntryIter(jobId);
        if(iter != this->endJob()) {
            return *iter;
        }
        return nullptr;
    }

    void send(int sigNum) const {
        for(auto begin = this->beginJob(); begin != this->endJob(); ++begin) {
            (*begin)->send(sigNum);
        }
    }

    unsigned int size() const {
        return this->entries.size();
    }

    // helper method for entry lookup
    ConstEntryIter beginJob() const {
        return this->entries.begin();
    }

    ConstEntryIter endJob() const {
        return this->entries.begin() + this->jobSize;
    }

private:
    EntryIter beginJob() {
        return this->entries.begin();
    }

    EntryIter endJob() {
        return this->entries.begin() + this->jobSize;
    }

    /**
     *
     * @return
     * entry index.
     * new job id is index + 1
     */
    unsigned int findEmptyEntry() const;

    /**
     *
     * @param jobId
     * greater than 0.
     * @return
     * if not found, return end
     */
    ConstEntryIter findEntryIter(unsigned int jobId) const;

    /**
     *
     * @param iter
     * @return
     * iterator of next entry.
     */
    EntryIter detachByIter(ConstEntryIter iter);
};

} // namespace ydsh

#endif //YDSH_JOB_H
