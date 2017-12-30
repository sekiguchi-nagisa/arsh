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

#ifndef YDSH_JOB_H
#define YDSH_JOB_H

#include <unistd.h>
#include <signal.h>

#include <vector>

#include "misc/resource.hpp"

struct DSState;

namespace ydsh {

/**
 * after fork, reset signal setting in child process.
 */
pid_t xfork(DSState &st, pid_t pgid, bool foreground);

void tryToForeground(const DSState &st);

class JobTable;

struct JobTrait;

enum class ProcState : unsigned char {
    RUNNING,
    STOPPED,    // stopped by SIGSTOP or SIGTSTP
    TERMINATED, // already called waitpid
};

struct Proc {
    pid_t pid;
    ProcState state;

    /**
     * enabled when `state' is TERMINATED.
     */
    unsigned char exitStatus;
};

class JobImpl {
private:
    unsigned long refCount{0};

    /**
     * after detach, will be 0
     */
    unsigned int jobId;

    /**
     * pid of owner process (JobEntry creator)
     */
    const pid_t ownerPid;

    unsigned short procSize;

    /**
     * if all process are terminated. will be TERMINATED
     */
    ProcState state{ProcState::RUNNING};

    /**
     * if already closed, will be -1.
     */
    int oldStdin{-1};

    /**
     * initial size is procSize + 1 (due to append process)
     */
    Proc procs[];

    friend class JobTable;

    friend struct JobTrait;

    JobImpl(unsigned int jobId, unsigned int size, pid_t *pids, bool saveStdin) :
            jobId(jobId), ownerPid(getpid()), procSize(size) {
        for(unsigned int i = 0; i < this->procSize; i++) {
            this->procs[i] = {
                    .pid = pids[i],
                    .state = ProcState::RUNNING,
                    .exitStatus = 0,
            };
        }
        if(saveStdin) {
            this->oldStdin = dup(STDIN_FILENO);
        }
    }

    ~JobImpl() = default;

public:
    NON_COPYABLE(JobImpl);

    unsigned short getProcSize() const {
        return this->procSize;
    }

    bool available() const {
        return this->state != ProcState::TERMINATED;
    }

    /**
     *
     * @param index
     * @return
     * after termiantion, return -1.
     */
    pid_t getPid(unsigned int index) const {
        return this->procs[index].pid;
    }

    /**
     *
     * @return
     * after detached, return 0.
     */
    unsigned int getJobId() const {
        return this->jobId;
    }

    pid_t getOwnerPid() const {
        return this->ownerPid;
    }

    bool hasOwnership() const {
        return this->ownerPid == getpid();
    }

    /**
     * call only once.
     * @param pid
     */
    void appendPid(pid_t pid) {
        assert(this->procSize > 0);
        this->procs[this->procSize++] = {
                .pid = pid,
                .state = ProcState::RUNNING,
                .exitStatus = 0,
        };
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
     * @param sigNum
     */
    void send(int sigNum);

    /**
     * wait for termination.
     * after termination, `state' will be TERMINATED.
     * @return
     * exit status of last process.
     * if cannot terminate (has no-wnership or stopped), return -1.
     */
    int wait();
};

struct JobTrait {
    static unsigned long useCount(const JobImpl *ptr) noexcept {
        return ptr->refCount;
    }

    static void increase(JobImpl *ptr) noexcept {
        if(ptr != nullptr) {
            ptr->refCount++;
        }
    }

    static void decrease(JobImpl *ptr) noexcept {
        if(ptr != nullptr && --ptr->refCount == 0) {
            free(ptr);
        }
    }
};

using Job = IntrusivePtr<JobImpl, JobTrait>;

class JobTable {    //FIXME: send signal to managed jobs
private:
    std::vector<Job> entries;

    /**
     * cache latest JobEntry.
     */
    Job latestEntry;

public:
    JobTable() = default;
    ~JobTable() = default;

    Job newEntry(unsigned int size, pid_t *pids, bool saveStdin = true);

    Job newEntry(pid_t pid) {
        pid_t pids[1] = {pid};
        return newEntry(1, pids, false);
    }

    /**
     * remove job from JobTable
     * @param jobId
     * if 0, do nothing.
     * @return
     * detached job.
     * if specified job is not found, return null
     */
    Job detach(unsigned int jobId);

    /**
     * if has ownership, wait termination.
     * @param entry
     * @return
     * exit status of last process.
     * after waiting termination, remove entry.
     */
    int waitAndDetach(Job &entry) {
        int ret = entry->wait();
        if(!entry->available()) {
            this->detach(entry->getJobId());
        }
        return ret;
    }

    void detachAll() {
        for(auto &e : this->entries) {
            e->jobId = 0;
        }
        this->entries.clear();
        this->latestEntry.reset();
    }

    Job &getLatestEntry() {
        return this->latestEntry;
    }

    std::vector<Job>::iterator begin() {
        return this->entries.begin();
    }

    std::vector<Job>::iterator end() {
        return this->entries.end();
    }

    /**
     *
     * @param jobId
     * @return
     * if not found, return nullptr
     */
    Job findEntry(unsigned int jobId) const;

private:
    /**
     *
     * @return
     * first is entry index.
     * second is job id.
     */
    std::pair<unsigned int, unsigned int> findEmptyEntry() const;   //FIXME: binary search

    std::vector<Job>::const_iterator findEntryIter(unsigned int jobId) const;
};

} // namespace ydsh

#endif //YDSH_JOB_H
