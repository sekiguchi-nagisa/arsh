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

#include <sys/wait.h>

#include <algorithm>

#include "vm.h"

namespace ydsh {

pid_t xfork(DSState &st, pid_t pgid, bool foreground) {
    pid_t pid = fork();
    if(pid == 0) {  // child process
        if(st.isInteractive()) {
            setpgid(0, pgid);
            if(foreground) {
                tcsetpgrp(STDIN_FILENO, getpgid(0));
            }

            struct sigaction act{};
            act.sa_handler = SIG_DFL;
            act.sa_flags = 0;
            sigemptyset(&act.sa_mask);

            /**
             * reset signal behavior
             */
            sigaction(SIGINT, &act, nullptr);
            sigaction(SIGQUIT, &act, nullptr);
            sigaction(SIGTSTP, &act, nullptr);
            sigaction(SIGTTIN, &act, nullptr);
            sigaction(SIGTTOU, &act, nullptr);
            sigaction(SIGCHLD, &act, nullptr);
        }

        // update PID, PPID
        st.setGlobal(toIndex(BuiltinVarOffset::PID), DSValue::create<Int_Object>(st.pool.getInt32Type(), getpid()));
        st.setGlobal(toIndex(BuiltinVarOffset::PPID), DSValue::create<Int_Object>(st.pool.getInt32Type(), getppid()));
    } else if(pid > 0) {
        if(st.isInteractive()) {
            setpgid(pid, pgid);
            if(foreground) {
                tcsetpgrp(STDIN_FILENO, getpgid(pid));
            }
        }
    }
    return pid;
}

void tryToForeground(const DSState &st) {
    if(st.isForeground()) {
        tcsetpgrp(STDIN_FILENO, getpgid(0));
    }
}

// ##########################
// ##     JobEntryImpl     ##
// ##########################

void JobEntryImpl::restoreStdin() {
    if(this->oldStdin > -1 && this->hasOwnership()) {
        dup2(this->oldStdin, STDIN_FILENO);
        close(this->oldStdin);
    }
}

// ######################
// ##     JobTable     ##
// ######################

JobEntry JobTable::newEntry(unsigned int size) {
    assert(size > 0);

    void *ptr = malloc(sizeof(JobEntryImpl) + sizeof(pid_t) * size);
    auto pair = this->findEmptyEntry();
    auto *entry = new(ptr) JobEntryImpl(pair.second, size);
    auto v = JobEntry(entry);
    this->entries.insert(this->entries.begin() + pair.first, v);
    this->latestEntry = v;
    return v;
}

int JobTable::forceWait(JobEntry &entry, unsigned int statusSize, int *statuses) {
    if(entry->procSize == 0) {
        return -1;
    }

    // wait termination
    int lastStatus = 0;
    for(unsigned int i = 0; i < entry->procSize; i++) {
        pid_t pid = entry->pids[i];
        entry->pids[i] = -1;
        int status = -1;
        if(pid > -1) {
            waitpid(pid, &status, 0);
            if(WIFEXITED(status)) {
                status = WEXITSTATUS(status);
            } else if(WIFSIGNALED(status)) {
                status = WTERMSIG(status) + 128;
            }
        }
        if(i < statusSize) {
            statuses[i] = status;
        }
        lastStatus = status;
    }
    entry->procSize = 0;

    // remove entry
    auto iter = this->findEntryIter(entry->getJobId());
    if(iter != this->entries.end() && (*iter) == entry) {
        this->entries.erase(iter);
    }
    return lastStatus;
}

std::pair<unsigned int, unsigned int> JobTable::findEmptyEntry() const {
    const unsigned int size = this->entries.size();
    if(size == 0) {
        return {0, 1};
    }

    if(this->entries.back()->jobId == size) {
        return {size, size + 1};
    }

    /**
     *  | 3 | 4 |
     *
     *  | 1 | 4 |
     *
     *  | 1 | 2 | 3 | 4 | 7 |
     */
    for(unsigned int i = 0; i < size; i++) {
        if(this->entries[i]->jobId != i + 1) {
            return {i, i + 1};  //FIXME: optimize lookup
        }
    }

    fatal("normally unreachable\n");
}

struct Comparator {
    bool operator()(const JobEntry &x, unsigned int y) const {
        return x->getJobId() < y;
    }

    bool operator()(unsigned int x, const JobEntry &y) const {
        return x < y->getJobId();
    }
};

auto JobTable::findEntryIter(unsigned int jobId) const -> decltype(this->entries.end()) {
    return std::lower_bound(this->entries.begin(), this->entries.end(), jobId, Comparator());
}

JobEntry JobTable::findEntry(unsigned int jobId) const {
    auto iter = this->findEntryIter(jobId);
    if(iter != this->entries.end() && (*iter)->jobId == jobId) {
        return *iter;
    }
    return nullptr;
}

} // namespace ydsh