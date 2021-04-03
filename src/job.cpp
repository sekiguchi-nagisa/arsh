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
                beForeground(0);
            }
            setJobControlSignalSetting(st, false);
        }

        // clear queued signal
        DSState::clearPendingSignal();
        unsetFlag(DSState::eventDesc, VMEvent::MASK);

        // clear JobTable entries
        st.jobTable.detachAll();

        // clear signal handler
        st.sigVector.clear();

        // clear termination hook
        st.setGlobal(st.builtinModScope->lookup(VAR_TERM_HOOK)->getIndex(), DSValue::createInvalid());

        // update PID, PPID
        st.setGlobal(BuiltinVarOffset::PID, DSValue::createInt(getpid()));
        st.setGlobal(BuiltinVarOffset::PPID, DSValue::createInt(getppid()));

        st.subshellLevel++;
    } else if(pid > 0) {
        if(st.isJobControl()) {
            setpgid(pid, pgid);
            if(foreground) {
                beForeground(pid);
            }
        }
    }
    return Proc(pid);
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

//#ifdef USE_LOGGING
static const char *toString(Proc::WaitOp op) {
    const char *str = nullptr;
    switch(op) {
#define GEN_STR(OP) case Proc::OP: str = #OP; break;
    EACH_WAIT_OP(GEN_STR)
#undef GEN_STR
    }
    return str;
}
//#endif

int Proc::wait(WaitOp op, bool showSignal) {
    if(this->state() != TERMINATED) {
        int status = 0;
        errno = 0;
        int ret = waitpid(this->pid_, &status, toOption(op));
        int errNum = errno;

        // dump waitpid result
        LOG_EXPR(DUMP_WAIT, [&]{
            std::string str;
            char *str1 = nullptr;
            if(asprintf(&str1, "opt: %s\npid: %d, before state: %s\nret: %d",
                    toString(op), this->pid(), this->state() == Proc::RUNNING ? "RUNNING" : "STOPPED", ret) != -1) {
               str = str1;
               free(str1);
            }
            if(ret > 0) {
                str += "\nafter state: ";
                if(WIFEXITED(status)) {
                    str += "TERMINATED\nkind: EXITED, status: ";
                    str += std::to_string(WEXITSTATUS(status));
                } else if(WIFSIGNALED(status)) {
                    int sigNum = WTERMSIG(status);
                    str += "TERMINATED\nkind: SIGNALED, status: ";
                    str += getSignalName(sigNum);
                    str += "(";
                    str += std::to_string(sigNum);
                    str += ")";
                } else if(WIFSTOPPED(status)) {
                    int sigNum = WSTOPSIG(status);
                    str += "STOPPED\nkind: STOPPED, status: ";
                    str += getSignalName(sigNum);
                    str += "(";
                    str += std::to_string(sigNum);
                    str += ")";
                } else if(WIFCONTINUED(status)) {
                    str += "RUNNING\nkind: CONTINUED";
                }
            } else if(ret < 0) {
                str += "\nFAILED\n";
                str += strerror(errNum);
            }
            return str;
        });

        if(ret > 0) {
            // update status
            if(WIFEXITED(status)) {
                this->state_ = TERMINATED;
                this->exitStatus_ = WEXITSTATUS(status);
            } else if(WIFSIGNALED(status)) {
                int sigNum = WTERMSIG(status);
                bool hasCoreDump = false;
                this->state_ = TERMINATED;
                this->exitStatus_ = sigNum + 128;

#ifdef WCOREDUMP
                if(WCOREDUMP(status)) {
                    hasCoreDump = true;
                }
#endif
                if(showSignal) {
                    fprintf(stderr, "%s%s\n", strsignal(sigNum), hasCoreDump ? " (core dumped)" : "");
                    fflush(stderr);
                }
            } else if(WIFSTOPPED(status)) {
                this->state_ = STOPPED;
                this->exitStatus_ = WSTOPSIG(status) + 128;
            } else if(WIFCONTINUED(status)) {
                this->state_ = RUNNING;
            }

            if(this->state_ == TERMINATED) {
                this->pid_ = -1;
            }
        } else if(ret < 0) {
            errno = errNum;
            return -1;
        }
    }
    return this->exitStatus_;
}

int Proc::send(int sigNum) const {
    if(this->pid() > 0) {
        return kill(this->pid(), sigNum);
    }
    return 0;
}


// #####################
// ##     JobImpl     ##
// #####################

bool JobObject::restoreStdin() {
    if(this->oldStdin > -1 && this->isControlled()) {
        dup2(this->oldStdin, STDIN_FILENO);
        close(this->oldStdin);
        this->oldStdin = -1;
        return true;
    }
    return false;
}

void JobObject::send(int sigNum) const {
    if(!this->available()) {
        return;
    }

    pid_t pid = this->getPid(0);
    if(pid == getpgid(pid)) {
        kill(-pid, sigNum);
        return;
    }
    for(unsigned int i = 0; i < this->procSize; i++) {
        this->procs[i].send(sigNum);
    }
}

int JobObject::wait(Proc::WaitOp op) {
    errno = 0;
    if(!isControlled()) {
        errno = ECHILD;
        return -1;
    }
    if(!this->available()) {
        return this->procs[this->procSize - 1].exitStatus();
    }

    unsigned int terminateCount = 0;
    int lastStatus = 0;
    for(unsigned short i = 0; i < this->procSize; i++) {
        auto &proc = this->procs[i];
        lastStatus = proc.wait(op, i == this->procSize - 1);
        if(lastStatus < 0) {
            return lastStatus;
        }
        if(proc.state() == Proc::TERMINATED) {
            terminateCount++;
        }
    }
    if(terminateCount == this->procSize) {
        this->state = State::TERMINATED;
    }
    if(!this->available()) {
        typeAs<UnixFdObject>(this->inObj).tryToClose(false);
        typeAs<UnixFdObject>(this->outObj).tryToClose(false);
    }
    return lastStatus;
}

// ######################
// ##     JobTable     ##
// ######################

void JobTable::attach(Job job, bool disowned) {
    if(job->getJobID() != 0) {
        return;
    }

    if(disowned) {
        this->entries.push_back(std::move(job));
        return;
    }

    auto ret = this->findEmptyEntry();
    this->entries.insert(this->beginJob() + ret, job);
    job->jobID = ret + 1;
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
        Job job = *iter;
        if(job->getJobID() > 0) {
            this->jobSize--;
        }
        job->jobID = 0;
        auto next = this->entries.erase(iter);

        // change latest entry
        if(this->latestEntry == job) {
            this->latestEntry = nullptr;
            if(!this->entries.empty()) {
                this->latestEntry = this->entries[this->jobSize - 1];
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

unsigned int JobTable::findEmptyEntry() const {
    unsigned int firstIndex = 0;
    unsigned int dist = this->jobSize;

    while(dist > 0) {
        unsigned int hafDist = dist / 2;
        unsigned int midIndex = hafDist + firstIndex;
        if(entries[midIndex]->getJobID() == midIndex + 1) {
            firstIndex = midIndex + 1;
            dist = dist - hafDist - 1;
        } else {
            dist = hafDist;
        }
    }
    return firstIndex;
}

struct Comparator {
    bool operator()(const Job &x, unsigned int y) const {
        return x->getJobID() < y;
    }

    bool operator()(unsigned int x, const Job &y) const {
        return x < y->getJobID();
    }
};

JobTable::ConstEntryIter JobTable::findEntryIter(unsigned int jobId) const {
    if(jobId > 0) {
        auto iter = std::lower_bound(this->beginJob(), this->endJob(), jobId, Comparator());
        if(iter != this->endJob() && (*iter)->jobID == jobId) {
            return iter;
        }
    }
    return this->endJob();
}

} // namespace ydsh