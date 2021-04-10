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
#include "misc/detect.hpp"
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

class ProcTable;

class JobTable;

class JobObject : public ObjectWithRtti<ObjectKind::Job> {
public:
    static_assert(is_pod_v<Proc>, "failed");

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
     * if already closed, will be -1.
     */
    int oldStdin{-1};

    /**
     * after termination will be 0
     */
    unsigned short jobID{0};

    State state{State::RUNNING};

    bool disown{false};

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

    bool isDisowned() const {
        return this->disown;
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
     * after terminated, return 0.
     */
    unsigned short getJobID() const {
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
     * @param procTable
     * may be null
     * @return
     * exit status of last process.
     * if cannot terminate (has no-ownership or has error), return -1 and set errno
     * if procTable is not null, after wait, remove terminated procs from procTable
     */
    int wait(Proc::WaitOp op, ProcTable *procTable = nullptr);
};

using Job = ObjPtr<JobObject>;

// for pid to job mapping
class ProcTable {
public:
    struct Entry {
        pid_t pid;
        unsigned short jobId;
        unsigned short procOffset;

        void markDelete() {
            this->jobId = 0;
        }

        bool isDeleted() const {
            return this->jobId == 0;
        }
    };

private:
    FlexBuffer<Entry> entries;

public:
    const FlexBuffer<Entry> &getEntries() const {
        return this->entries;
    }

    void add(const Job &job) {
        for(unsigned int i = 0; i < job->getProcSize(); i++) {
            this->addProc(job->getPid(i), job->getJobID(), i);
        }
    }

    const Entry *addProc(pid_t pid, unsigned short jobId, unsigned short offset);

    /**
     * call markDelete() in specified entry by pid.
     * actual delete operation is not performed untill call batchedRemove()
     * @param pid
     * @return
     * if found corresponding entry, return true
     */
    bool deleteProc(pid_t pid);

    void clear() {
        this->entries.clear();
    }

    /**
     * remove all deleted PidEntry
     */
    void batchedRemove();
};

class JobTable {
private:
    std::vector<Job> jobs;

    ProcTable procTable;

    /**
     * latest attached entry.
     */
    Job latest;

public:
    NON_COPYABLE(JobTable);

    using EntryIter = std::vector<Job>::iterator;
    using ConstEntryIter = std::vector<Job>::const_iterator;

    JobTable() = default;
    ~JobTable() = default;

    void attach(Job job, bool disowned = false);

    /**
     * detach job from JopTable
     * @param job
     * if terminated or uncontrolled, do nothing
     * @param remove
     * if true, completely remove from job table.
     * if false, job is disowned, but still exists in job table
     */
    void detach(Job &job, bool remove = false);

    /**
     * if has ownership, wait termination.
     * @param job
     * @param op
     * @return
     * exit status of last process.
     * after waiting termination, remove entry.
     */
    int waitAndDetach(Job &job, Proc::WaitOp op) {
        int ret = job->wait(op, &this->procTable);
        if(!job->available()) {
            this->detach(job, true);
        }
        return ret;
    }

    void detachAll() {
        for(auto &e : this->jobs) {
            e->jobID = 0;
            e->disown = true;
            e->state = JobObject::State::UNCONTROLLED;
        }
        this->jobs.clear();
        this->procTable.clear();
        this->latest.reset();
    }

    /**
     * update status of managed jobs.
     * when a job is terminated, detach job.
     * should call after wait termination of foreground job.
     */
    void updateStatus();

    const Job &getLatestJob() const {
        return this->latest;
    }

    /**
     * get Job by job id
     * @param jobId
     * @return
     * if not found, return nullptr
     * if job is disowned, return nullptr
     */
    Job find(unsigned int jobId) const {
        auto iter = this->findIter(jobId);
        if(iter == this->endJob() || (*iter)->isDisowned()) {
            return nullptr;
        }
        return *iter;
    }

    /**
     * send signal to all job except for disowned job
     * @param sigNum
     */
    void send(int sigNum) const {
        for(auto &job : this->jobs) {
            if(!job->isDisowned()) {
                job->send(sigNum);
            }
        }
    }

    unsigned int size() const {
        return this->jobs.size();
    }

    // helper method for entry lookup
    ConstEntryIter beginJob() const {
        return this->jobs.begin();
    }

    ConstEntryIter endJob() const {
        return this->jobs.end();
    }

    const ProcTable &getProcTable() const {
        return this->procTable;
    }

private:
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
    ConstEntryIter findIter(unsigned int jobId) const;

    /**
     * remove entry specified by ietrator
     * @param iter
     * @return
     * iterator of next entry.
     */
    EntryIter removeByIter(ConstEntryIter iter);
};

} // namespace ydsh

#endif //YDSH_JOB_H
