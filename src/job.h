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

class JobImpl {
private:
    unsigned long refCount{0};
    const unsigned int jobId;

    /**
     * pid of owner process (JobEntry creator)
     */
    const pid_t ownerPid;

    /**
     * after termination, procSize will be 0.
     */
    unsigned int procSize;

    /**
     * if already closed, will be -1.
     */
    int oldStdin;

    /**
     * initial size is procSize + 1 (due to append process)
     */
    pid_t pids[];

    friend class JobTable;

    friend struct JobTrait;

    JobImpl(unsigned int jobId, unsigned int size, bool saveStdin) :
            jobId(jobId), ownerPid(getpid()), procSize(size), oldStdin(-1) {
        for(unsigned int i = 0; i < this->procSize; i++) {
            this->pids[i] = -1;
        }
        if(saveStdin) {
            this->oldStdin = dup(STDIN_FILENO);
        }
    }

    ~JobImpl() = default;

public:
    NON_COPYABLE(JobImpl);

    unsigned int getProcSize() const {
        return this->procSize;
    }

    void setPid(unsigned int index, pid_t pid) {
        this->pids[index] = pid;
    }

    pid_t getPid(unsigned int index) const {
        return this->pids[index];
    }

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
        this->pids[this->procSize++] = pid;
    }

    /**
     * restore STDIN_FD
     * if has no ownership, do nothing.
     * @return
     * if restore fd, return true.
     * if already called, return false
     */
    bool restoreStdin();
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

    Job newEntry(unsigned int size, bool saveStdin = true);

    /**
     * if has ownership, wait termination.
     * @param entry
     * @param statusSize
     * @param statuses
     * @return
     * exit status of last process.
     * if job is already terminated, return -1.
     * after waiting termination, remove entry.
     */
    int wait(Job &entry, unsigned int statusSize, int *statuses);

    void detachAll() {
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

private:
    /**
     *
     * @return
     * first is entry index.
     * second is job id.
     */
    std::pair<unsigned int, unsigned int> findEmptyEntry() const;   //FIXME: binary search

    std::vector<Job>::const_iterator findEntryIter(unsigned int jobId) const;

    /**
     *
     * @param jobId
     * @return
     * if not found, return nullptr
     */
    Job findEntry(unsigned int jobId) const;
};

} // namespace ydsh

#endif //YDSH_JOB_H
