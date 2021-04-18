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

    if(st.jobTable.size() >= UINT16_MAX) {
        errno = EAGAIN;
        return Proc(-1);
    }

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

//#ifdef USE_LOGGING
static const char *toString(WaitOp op) {
    const char *str = nullptr;
    switch(op) {
#define GEN_STR(OP) case WaitOp::OP: str = #OP; break;
    EACH_WAIT_OP(GEN_STR)
#undef GEN_STR
    }
    return str;
}
//#endif

WaitResult waitForProc(pid_t pid, WaitOp op) {
    int option = 0;
    switch(op) {
    case WaitOp::BLOCKING:
        break;
    case WaitOp::BLOCK_UNTRACED:
        option = WUNTRACED;
        break;
    case WaitOp::NONBLOCKING:
        option = WUNTRACED | WCONTINUED | WNOHANG;
        break;
    }

    int status = 0;
    errno = 0;

    LOG_EXPR(DUMP_WAIT, [&]{
        std::string str;
        str = "waitpid(";
        str += std::to_string(pid);
        str += ", ";
        str += toString(op);
        str += ")";
        return str;
    });

    int ret = waitpid(pid, &status, option);
    int errNum = errno;

    // dump waitpid status
    LOG_EXPR(DUMP_WAIT, [&]{
        std::string str = "ret = ";
        str += std::to_string(ret);
        if(ret > 0) {
            str += "\nstate: ";
            if(WIFEXITED(status)) {
                str += "TERMINATED, kind: EXITED, status: ";
                str += std::to_string(WEXITSTATUS(status));
            } else if(WIFSIGNALED(status)) {
                int sigNum = WTERMSIG(status);
                str += "TERMINATED, kind: SIGNALED, status: ";
                str += getSignalName(sigNum);
                str += "(";
                str += std::to_string(sigNum);
                str += ")";
            } else if(WIFSTOPPED(status)) {
                int sigNum = WSTOPSIG(status);
                str += "STOPPED, kind: STOPPED, status: ";
                str += getSignalName(sigNum);
                str += "(";
                str += std::to_string(sigNum);
                str += ")";
            } else if(WIFCONTINUED(status)) {
                str += "RUNNING, kind: CONTINUED";
            }
        } else if(ret < 0) {
            str += "\nFAILED\n";
            str += strerror(errNum);
        }
        return str;
    });

    errno = errNum; //NOLINT
    return WaitResult {
        .pid = ret,
        .status = status,
    };
}

// ##################
// ##     Proc     ##
// ##################

bool Proc::updateState(WaitResult ret, bool showSignal) {
    if(this->is(State::TERMINATED) || ret.pid != this->pid()) {
        return false;
    }

    int status = ret.status;
    if(WIFEXITED(status)) {
        this->state_ = State::TERMINATED;
        this->exitStatus_ = WEXITSTATUS(status);
    } else if(WIFSIGNALED(status)) {
        int sigNum = WTERMSIG(status);
        bool hasCoreDump = false;
        this->state_ = State::TERMINATED;
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
        this->state_ = State::STOPPED;
        this->exitStatus_ = WSTOPSIG(status) + 128;
    } else if(WIFCONTINUED(status)) {
        this->state_ = State::RUNNING;
    }

    if(this->state_ == State::TERMINATED) {
        this->pid_ = -1;
    }
    return true;
}

int Proc::send(int sigNum) const {
    if(this->pid() > 0) {
        return kill(this->pid(), sigNum);
    }
    return 0;
}

// #######################
// ##     JobObject     ##
// #######################

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

int JobObject::wait(WaitOp op, ProcTable *procTable) {
    if(!isControlled()) {
        errno = ECHILD;
        return -1;
    }

    errno = 0;
    int errNum = errno;
    int lastStatus = 0;
    if(this->available()) {
        for(unsigned short i = 0; i < this->procSize; i++) {
            auto &proc = this->procs[i];
            pid_t pid = proc.pid();
            lastStatus = proc.wait(op, i == this->procSize - 1);
            if(lastStatus < 0) {
                return lastStatus;
            }
            if(proc.is(Proc::State::TERMINATED) && procTable) {
                procTable->deleteProc(pid);
            }
        }
        errNum = errno;
        this->updateState();
    }
    if(!this->available()) {
        typeAs<UnixFdObject>(this->inObj).tryToClose(false);
        typeAs<UnixFdObject>(this->outObj).tryToClose(false);
        errno = errNum;
        return this->procs[this->procSize - 1].exitStatus();
    }
    errno = errNum;
    return lastStatus;
}

// #######################
// ##     ProcTable     ##
// #######################

struct PidEntryComp {
    bool operator()(const ProcTable::Entry &x, pid_t y) const {
        return x.pid() < y;
    }

    bool operator()(pid_t x, const ProcTable::Entry &y) const {
        return x < y.pid();
    }
};

const ProcTable::Entry *ProcTable::addProc(pid_t pid, unsigned short jobId, unsigned short offset) {
    if(pid < 0 || jobId == 0) {
        return nullptr;
    }
    auto iter = std::lower_bound(this->entries.begin(), this->entries.end(), pid, PidEntryComp());
    if(iter == this->entries.end() || iter->pid() != pid) {
        auto ret = this->entries.insert(iter, ProcTable::Entry(pid, jobId, offset));
        return ret;
    }
    return nullptr;
}

ProcTable::Entry * ProcTable::findProc(pid_t pid) {
    if(pid > 0) {
        auto iter = std::lower_bound(this->entries.begin(), this->entries.end(), pid, PidEntryComp());
        if(iter != this->entries.end()) {
            return iter;
        }
    }
    return nullptr;
}

void ProcTable::batchedRemove() {
    if(this->deletedCount == 0) {
        return;
    }
    this->deletedCount = 0;

    unsigned int removedIndex;
    for(removedIndex = 0; removedIndex < this->entries.size(); removedIndex++) {
        if(this->entries[removedIndex].isDeleted()) {
            break;
        }
    }
    if(removedIndex == this->entries.size()) {
        return; // not found deleted entry
    }

    for(unsigned int i = removedIndex + 1; i < this->entries.size(); i++) {
        if(!this->entries[i].isDeleted()) {
            assert(this->entries[removedIndex].isDeleted());
            std::swap(this->entries[i], this->entries[removedIndex]);
            removedIndex++;
        }
    }
    this->entries.resize(removedIndex);
}

// ######################
// ##     JobTable     ##
// ######################

void JobTable::attach(Job job, bool disowned) {
    if(job->getJobID() != 0) {  // already attached
        return;
    }
    auto ret = this->findEmptyEntry();
    this->jobs.insert(this->jobs.begin() + ret, job);
    job->jobID = ret + 1;
    this->procTable.add(job);
    if(disowned) {
        job->disown = true;
    } else {
        this->latest = std::move(job);
    }
}

void JobTable::detach(Job &job, bool remove) {
    if(remove) {
        auto iter = this->findIter(job->getJobID());
        this->removeByIter(iter);
    } else {
        job->disown = true;
    }
}

JobTable::EntryIter JobTable::removeByIter(ConstEntryIter iter) {
    if(iter != this->jobs.end()) {
        Job job = *iter;
        assert(job->getJobID() > 0);
        job->jobID = 0;
        job->disown = true;
        auto next = this->jobs.erase(iter);

        // change latest entry
        if(this->latest == job) {
            this->latest = nullptr;
            for(auto i = this->jobs.rbegin(); i != this->jobs.rend(); ++i) {
                auto &j = *i;
                if(!j->isDisowned()) {
                    this->latest = j;
                    break;
                }
            }
        }
        return next;
    }
    return this->jobs.end();
}

void JobTable::waitForAny() {
    SignalGuard guard;
    DSState::clearPendingSignal(SIGCHLD);

    for(WaitResult ret; (ret = waitForProc(-1, WaitOp::NONBLOCKING)).pid != 0;) {
        if(ret.pid == -1) {
            if(errno != ECHILD) {
                fatal_perror("wait failed");    //FIXME: propagate error as exception
            }
            break;
        }
        this->updateProcState(ret);
    }
    this->procTable.batchedRemove();
    this->removeTerminatedJobs();
}

int JobTable::waitForProcOrJob(unsigned int size, ProcOrJob *targets, WaitOp op, bool ignoreError) {
    errno = 0;
    if(!targets) {
        for(auto &job : this->jobs) {
            job->wait(op, &this->procTable);
        }
        this->procTable.batchedRemove();
        this->removeTerminatedJobs();
        return 0;
    }

    int lastStatus = 0;
    for(unsigned int i = 0; i < size; i++) {
        if(is<pid_t>(targets[i])) {
            pid_t pid = get<pid_t>(targets[i]);
            WaitResult ret = waitForProc(pid, op);
            if(ret.pid == -1) {
                if(ignoreError) {
                    continue;
                }
                return -1;
            }
            if(const Proc *p; (p = this->updateProcState(ret))) {
                lastStatus = p->exitStatus();
            }
        } else if(is<Job>(targets[i])) {
            Job &job = get<Job>(targets[i]);
            lastStatus = job->wait(op, &this->procTable);
            if(lastStatus < 0) {
                return -1;
            }
        }
    }
    int errNum = errno;
    this->procTable.batchedRemove();
    this->removeTerminatedJobs();
    errno = errNum;
    return lastStatus;
}

unsigned int JobTable::findEmptyEntry() const {
    unsigned int firstIndex = 0;
    unsigned int dist = this->jobs.size();

    while(dist > 0) {
        unsigned int hafDist = dist / 2;
        unsigned int midIndex = hafDist + firstIndex;
        if(jobs[midIndex]->getJobID() == midIndex + 1) {
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

JobTable::ConstEntryIter JobTable::findIter(unsigned int jobId) const {
    if(jobId > 0) {
        auto iter = std::lower_bound(this->jobs.begin(), this->jobs.end(), jobId, Comparator());
        if(iter != this->jobs.end() && (*iter)->jobID == jobId) {
            return iter;
        }
    }
    return this->jobs.end();
}

const Proc *JobTable::updateProcState(WaitResult ret) {
    if(ProcTable::Entry *entry; (entry = this->procTable.findProc(ret.pid))) {
        assert(!entry->isDeleted());
        auto iter = this->findIter(entry->jobId());
        assert(iter != this->jobs.end());
        auto &job = *iter;
        bool showSignal = entry->procOffset() == job->getProcSize() - 1;
        auto &proc = job->procs[entry->procOffset()];
        proc.updateState(ret, showSignal);
        if(proc.is(Proc::State::TERMINATED)) {
            this->procTable.deleteProc(*entry);
        }
        job->updateState();
        return &proc;
    }
    return nullptr;
}

void JobTable::removeTerminatedJobs() {
    unsigned int removedIndex;
    for(removedIndex = 0; removedIndex < this->jobs.size(); removedIndex++) {
        if(!this->jobs[removedIndex]->available()) {
            break;
        }
    }
    if(removedIndex == this->jobs.size()) {
        return; // not found deleted entry
    }

    for(unsigned int i = removedIndex + 1; i < this->jobs.size(); i++) {
        if(this->jobs[i]->available()) {
            assert(!this->jobs[removedIndex]->available());
            std::swap(this->jobs[i], this->jobs[removedIndex]);
            removedIndex++;
        }
    }
    assert(removedIndex < this->jobs.size());
    for(; this->jobs.size() != removedIndex; this->jobs.pop_back()) {
        auto &job = this->jobs.back();
        job->jobID = 0;
        job->disown = true;
    }

    // change latest entry
    if(this->latest && !this->latest->available()) {
        this->latest = nullptr;
        for(auto i = this->jobs.rbegin(); i != this->jobs.rend(); ++i) {
            auto &j = *i;
            if(!j->isDisowned()) {
                this->latest = j;
                break;
            }
        }
    }
}

} // namespace ydsh