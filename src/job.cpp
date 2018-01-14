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

#include <sys/wait.h>

#include <algorithm>
#include <cerrno>
#include <csignal>

#include "vm.h"
#include "logger.h"

namespace ydsh {

Proc Proc::fork(DSState &st, pid_t pgid, bool foreground) {
    SignalGuard guard;

    pid_t pid = ::fork();
    if(pid == 0) {  // child process
        if(st.isJobControl()) {
            setpgid(0, pgid);
            if(foreground) {
                tcsetpgrp(STDIN_FILENO, getpgid(0));
            }
            setJobControlSignalSetting(st, false);
        }

        // clear queued signal
        DSState::pendingSigIndex = 1;
        DSState::pendingSigSet.clear();
        unsetFlag(DSState::eventDesc, DSState::VM_EVENT_SIGNAL | DSState::VM_EVENT_MASK);

        // clear JobTable entries
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
    return Proc(pid);
}

void tryToForeground(const DSState &st) {
    if(st.isForeground()) {
        tcsetpgrp(STDIN_FILENO, getpgid(0));
    }
}

// ##################
// ##     Proc     ##
// ##################

static int toOption(Proc::WaitOp op) {
    int option = 0;
    switch(op) {
    case Proc::BLOCKING:
        break;
    case Proc::BLOCK_UNTRACED:
        option = WUNTRACED;
        break;
    case Proc::NONBLOCKING:
        option = WUNTRACED | WCONTINUED | WNOHANG;
        break;
    }
    return option;
}

#ifdef USE_LOGGING
static const char *toString(Proc::WaitOp op) {
    const char *str;
    switch(op) {
#define GEN_STR(OP) case Proc::OP: str = #OP; break;
    EACH_WAIT_OP(GEN_STR)
#undef GEN_STR
    }
    return str;
}
#endif

int Proc::wait(WaitOp op) {
    if(this->state() != TERMINATED) {
        int status = 0;
        int ret = waitpid(this->pid_, &status, toOption(op));
        if(ret == -1) {
            fatal("%s\n", strerror(errno));
        }

        // dump waitpid result
        LOG_L(DUMP_WAIT, [&](std::ostream &stream) {
            stream << "opt: " << toString(op) << std::endl;
            stream << "pid: " << this->pid() << ", before state: "
                   << (this->state() == Proc::RUNNING ? "RUNNING" : "STOPPED") << std::endl;
            stream << "ret: " << ret << std::endl;
            if(ret > 0) {
                stream << "after state: ";
                if(WIFEXITED(status)) {
                    stream << "TERMINATED" << std::endl
                           << "kind: EXITED, status: " << WEXITSTATUS(status);
                } else if(WIFSIGNALED(status)) {
                    int sigNum = WTERMSIG(status);
                    stream << "TERMINATED" << std::endl
                           << "kind: SIGNALED, status: " << getSignalName(sigNum) << "(" << sigNum << ")";
                } else if(WIFSTOPPED(status)) {
                    int sigNum = WSTOPSIG(status);
                    stream << "STOPPED" << std::endl
                           << "kind: STOPPED, status: " << getSignalName(sigNum) << "(" << sigNum << ")";
                } else if(WIFCONTINUED(status)) {
                    stream << "RUNNING" << std::endl << "kind: CONTINUED";
                }
                stream << std::endl;
            }
        });

        if(ret > 0) {
            // update status
            if(WIFEXITED(status)) {
                this->state_ = TERMINATED;
                this->exitStatus_ = WEXITSTATUS(status);
            } else if(WIFSIGNALED(status)) {
                this->state_ = TERMINATED;
                this->exitStatus_ = WTERMSIG(status) + 128;
            } else if(WIFSTOPPED(status)) {
                this->state_ = STOPPED;
                this->exitStatus_ = WSTOPSIG(status) + 128;
            } else if(WIFCONTINUED(status)) {
                this->state_ = RUNNING;
            }

            if(this->state_ == TERMINATED) {
                this->pid_ = -1;
            }
        }
    }
    return this->exitStatus_;
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

void JobImpl::send(int sigNum) const {
    if(!this->available()) {
        return;
    }

    pid_t pid = this->getPid(0);
    if(pid == getpgid(pid)) {
        kill(-pid, sigNum);
        return;
    }
    for(unsigned int i = 0; i < this->procSize; i++) {
        pid = this->getPid(i);
        if(pid > 0) {
            kill(pid, sigNum);
        }
    }
}

int JobImpl::wait(Proc::WaitOp op) {
    if(!hasOwnership()) {
        return -1;
    }
    if(!this->available()) {
        return this->procs[this->procSize - 1].exitStatus();
    }

    unsigned int terminateCount = 0;
    unsigned int lastStatus = 0;
    for(unsigned int i = 0; i < this->procSize; i++) {
        auto &proc = this->procs[i];
        int s = proc.wait(op);
        if(proc.state() == Proc::TERMINATED) {
            terminateCount++;
            lastStatus = s;
        }
    }
    if(terminateCount == this->procSize) {
        this->running = false;
    }
    return lastStatus;
}

// ######################
// ##     JobTable     ##
// ######################

void JobTable::attach(Job job, bool disowned) {
    if(job->jobID() != 0) {
        return;
    }

    if(disowned) {
        this->entries.push_back(std::move(job));
        return;
    }

    auto pair = this->findEmptyEntry();
    this->entries.insert(this->beginJob() + pair.first, job);
    job->jobID_ = pair.second;
    this->latestEntry = std::move(job);
    this->jobSize++;
}

Job JobTable::detach(unsigned int jobId, bool remove) {
    auto iter = this->findEntryIter(jobId);
    if(iter == this->endJob()) {
        return nullptr;
    }
    auto job = *iter;
    this->detachByIter(iter);
    if(!remove) {
        this->entries.push_back(job);
    }
    return job;
}

JobTable::EntryIter JobTable::detachByIter(ConstEntryIter iter) {
    if(iter != this->entries.end()) {
        /**
         * convert const_iterator -> iterator
         */
        auto actual = this->entries.begin() + (iter - this->entries.cbegin());
        Job job = *actual;
        if(job->jobID() > 0) {
            this->jobSize--;
        }
        job->jobID_ = 0;

        /**
         * in C++11, vector::erase accepts const_iterator.
         * but in libstdc++ 4.8, vector::erase(const_iterator) is not implemented.
         */
        auto next = this->entries.erase(actual);

        // change latest entry
        if(this->latestEntry == job) {
            this->latestEntry = nullptr;
            if(!this->entries.empty()) {
                this->latestEntry = this->entries.back();
            }
        }
        return next;
    }
    return this->entries.end();
}

void JobTable::updateStatus() {
    for(auto begin = this->entries.begin(); begin != this->entries.end();) {
        (*begin)->wait(Proc::NONBLOCKING);
        if((*begin)->available()) {
            ++begin;
        } else {
            begin = this->detachByIter(begin);
        }
    }
}

std::pair<unsigned int, unsigned int> JobTable::findEmptyEntry() const {
    const unsigned int size = this->jobSize;
    if(size == 0) {
        return {0, 1};
    }

    if(this->entries[size - 1]->jobID() == size) {
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
        if(this->entries[i]->jobID() != i + 1) {
            return {i, i + 1};  //FIXME: optimize lookup
        }
    }

    fatal("normally unreachable\n");
}

struct Comparator {
    bool operator()(const Job &x, unsigned int y) const {
        return x->jobID() < y;
    }

    bool operator()(unsigned int x, const Job &y) const {
        return x < y->jobID();
    }
};

JobTable::ConstEntryIter JobTable::findEntryIter(unsigned int jobId) const {
    if(jobId > 0) {
        auto iter = std::lower_bound(this->beginJob(), this->endJob(), jobId, Comparator());
        if(iter != this->endJob() && (*iter)->jobID_ == jobId) {
            return iter;
        }
    }
    return this->endJob();
}

} // namespace ydsh