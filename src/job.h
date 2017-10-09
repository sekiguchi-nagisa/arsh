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

class JobEntryImpl {
private:
    unsigned int refCount{0};
    const unsigned int jobId; //FIXME: tcsetgprp, STDIN_FD

    /**
     * pid of owner process (JobEntry creator)
     */
    const pid_t ownerPid;

    /**
     * after termination, procSize will be 0.
     */
    unsigned int procSize;

    pid_t pids[0];

    /**
     * if already closed, will be -1.
     */
    int oldStdin;

    friend class JobTable;

    NON_COPYABLE(JobEntryImpl);

    JobEntryImpl(unsigned int jobId, unsigned int size) : jobId(jobId), ownerPid(getpid()), procSize(size) {
        for(unsigned int i = 0; i < this->procSize; i++) {
            this->pids[i] = -1;
        }
        this->oldStdin = dup(STDIN_FILENO);
    }

    ~JobEntryImpl() = default;

public:
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
     * restore STDIN_FD.
     * if already called, do nothing.
     * if has no ownership. do nothing.
     */
    void restoreStdin();

    friend void intrusivePtr_addRef(JobEntryImpl *entry) noexcept {
        if(entry != nullptr) {
            entry->refCount++;
        }
    }

    friend void intrusivePtr_release(JobEntryImpl *entry) noexcept {
        if(entry != nullptr && --entry->refCount == 0) {
            free(entry);
        }
    }
};

using JobEntry = IntrusivePtr<JobEntryImpl>;

class JobTable {    //FIXME: send signal to managed jobs
private:
    std::vector<JobEntry> entries;

    /**
     * cache latest JobEntry.
     */
    JobEntry latestEntry;

public:
    JobTable() = default;
    ~JobTable() = default;

    JobEntry newEntry(unsigned int size);


    /**
     * if has ownership, wait termination.
     * @param entry
     * @param statusSize
     * @param statuses
     * @return
     */
    int wait(JobEntry &entry, unsigned int statusSize, int *statuses) {
        if(entry->hasOwnership()) {
            return this->forceWait(entry, statusSize, statuses);
        }
        return -1;
    }

    /**
     * force wait job termination.
     * @param entry
     * @param statusSize
     * @param statuses
     * @return
     * exit status of last process.
     * if job is already terminated, return -1.
     * after waiting termination, remove entry.
     */
    int forceWait(JobEntry &entry, unsigned int statusSize, int *statuses);


    JobEntry &getLatestEntry() {
        return this->latestEntry;
    }

private:
    /**
     *
     * @return
     * first is entry index.
     * second is job id.
     */
    std::pair<unsigned int, unsigned int> findEmptyEntry() const;   //FIXME: binary search

    auto findEntryIter(unsigned int jobId) const -> decltype(this->entries.end());

    /**
     *
     * @param jobId
     * @return
     * if not found, return nullptr
     */
    JobEntry findEntry(unsigned int jobId) const;
};

} // namespace ydsh

#endif //YDSH_JOB_H
