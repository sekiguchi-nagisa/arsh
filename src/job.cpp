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
    SignalGuard guard;

    pid_t pid = fork();
    if(pid == 0) {  // child process
        if(st.isJobControl()) {
            setpgid(0, pgid);
            if(foreground) {
                tcsetpgrp(STDIN_FILENO, getpgid(0));
            }
            setJobControlSignalSetting(st, false);
        }

        // clear queued signal
        DSState::signalQueue.clear();
        unsetFlag(DSState::eventDesc, DSState::VM_EVENT_SIGNAL | DSState::VM_EVENT_MASK);

        // clear JobTable entreis
        st.jobTable.detachAll();

        // update PID, PPID
        st.setGlobal(toIndex(BuiltinVarOffset::PID), DSValue::create<Int_Object>(st.pool.getInt32Type(), getpid()));
        st.setGlobal(toIndex(BuiltinVarOffset::PPID), DSValue::create<Int_Object>(st.pool.getInt32Type(), getppid()));
    } else if(pid > 0) {
        if(st.isJobControl()) {
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

// #####################
// ##     JobImpl     ##
// #####################

bool JobImpl::restoreStdin() {
    if(this->oldStdin > -1 && this->hasOwnership()) {
        dup2(this->oldStdin, STDIN_FILENO);
        close(this->oldStdin);
        return true;
    }
    return false;
}

// ######################
// ##     JobTable     ##
// ######################

Job JobTable::newEntry(unsigned int size, bool saveStdin) {
    assert(size > 0);

    void *ptr = malloc(sizeof(JobImpl) + sizeof(pid_t) * (size + 1));
    auto pair = this->findEmptyEntry();
    auto *entry = new(ptr) JobImpl(pair.second, size, saveStdin);
    auto v = Job(entry);
    this->entries.insert(this->entries.begin() + pair.first, v);
    this->latestEntry = v;
    return v;
}

int JobTable::forceWait(Job &entry, unsigned int statusSize, int *statuses) {
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
            waitpid(pid, &status, WUNTRACED);
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
    if(iter != this->entries.end()) {
        /**
         * convert const_iterator -> iterator
         */
        auto actual = this->entries.begin() + (iter - this->entries.cbegin());

        /**
         * in C++11, vector::erase accepts const_iterator.
         * but in libstdc++ 4.8, vector::erase(const_iterator) is not implemented.
         */
        this->entries.erase(actual);
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
    bool operator()(const Job &x, unsigned int y) const {
        return x->getJobId() < y;
    }

    bool operator()(unsigned int x, const Job &y) const {
        return x < y->getJobId();
    }
};

std::vector<Job>::const_iterator JobTable::findEntryIter(unsigned int jobId) const {
    auto iter = std::lower_bound(this->entries.cbegin(), this->entries.cend(), jobId, Comparator());
    if(iter != this->entries.end() && (*iter)->jobId == jobId) {
        return iter;
    }
    return this->entries.end();
}

Job JobTable::findEntry(unsigned int jobId) const {
    auto iter = this->findEntryIter(jobId);
    if(iter != this->entries.end()) {
        return *iter;
    }
    return nullptr;
}

} // namespace ydsh