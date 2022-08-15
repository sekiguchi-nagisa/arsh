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

#include "logger.h"
#include "misc/format.hpp"
#include "redir.h"
#include "vm.h"

namespace ydsh {

Proc Proc::fork(DSState &st, pid_t pgid, const Proc::Op op) {
  SignalGuard guard;

  // flush standard stream due to prevent mixing io buffer
  flushStdFD();

  if (st.jobTable.size() >= UINT16_MAX) {
    errno = EAGAIN;
    return Proc(-1);
  }

  pid_t pid = ::fork();
  if (pid == 0) { // child process
    if (hasFlag(op, Op::JOB_CONTROL)) {
      setpgid(0, pgid);
      if (hasFlag(op, Op::FOREGROUND)) {
        beForeground(0);
      }
      setJobControlSignalSetting(st, false);
    }

    // reset signal setting
    DSState::clearPendingSignal();
    unsetFlag(DSState::eventDesc, VMEvent::MASK);
    resetSignalSettingUnblock(st);

    // clear JobTable entries
    st.jobTable.loseControlOfJobs();

    // clear termination hook
    assert(st.termHookIndex != 0);
    st.setGlobal(st.termHookIndex, DSValue::createInvalid());

    // update PID, PPID
    st.setGlobal(BuiltinVarOffset::PID, DSValue::createInt(getpid()));
    st.setGlobal(BuiltinVarOffset::PPID, DSValue::createInt(getppid()));

    st.subshellLevel++;
  } else if (pid > 0) {
    if (hasFlag(op, Op::JOB_CONTROL)) {
      setpgid(pid, pgid);
      if (hasFlag(op, Op::FOREGROUND)) {
        beForeground(pid);
      }
    }
  }
  return Proc(pid);
}

// #ifdef USE_LOGGING
static const char *toString(WaitOp op) {
  const char *str = nullptr;
  switch (op) {
#define GEN_STR(OP)                                                                                \
  case WaitOp::OP:                                                                                 \
    str = #OP;                                                                                     \
    break;
    EACH_WAIT_OP(GEN_STR)
#undef GEN_STR
  }
  return str;
}
// #endif

WaitResult waitForProc(pid_t pid, WaitOp op) {
  int option = 0;
  switch (op) {
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

  LOG_EXPR(DUMP_WAIT, [&] {
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
  LOG_EXPR(DUMP_WAIT, [&] {
    std::string str = "ret = ";
    str += std::to_string(ret);
    if (ret > 0) {
      str += "\nstate: ";
      if (WIFEXITED(status)) {
        str += "TERMINATED, kind: EXITED, status: ";
        str += std::to_string(WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        int sigNum = WTERMSIG(status);
        str += "TERMINATED, kind: SIGNALED, status: ";
        str += getSignalName(sigNum);
        str += "(";
        str += std::to_string(sigNum);
        str += ")";
      } else if (WIFSTOPPED(status)) {
        int sigNum = WSTOPSIG(status);
        str += "STOPPED, kind: STOPPED, status: ";
        str += getSignalName(sigNum);
        str += "(";
        str += std::to_string(sigNum);
        str += ")";
      } else if (WIFCONTINUED(status)) {
        str += "RUNNING, kind: CONTINUED";
      }
    } else if (ret < 0) {
      str += "\nFAILED\n";
      str += strerror(errNum);
    }
    return str;
  });

  errno = errNum; // NOLINT
  return WaitResult{
      .pid = ret,
      .status = status,
  };
}

// ##################
// ##     Proc     ##
// ##################

bool Proc::updateState(WaitResult ret) {
  assert(ret.pid == this->pid());
  if (this->is(State::TERMINATED)) {
    return false;
  }

  int status = ret.status;
  if (WIFEXITED(status)) {
    this->state_ = State::TERMINATED;
    this->exitStatus_ = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    int sigNum = WTERMSIG(status);
    this->state_ = State::TERMINATED;
    this->exitStatus_ = sigNum + 128;
    this->signaled_ = true;
#ifdef WCOREDUMP
    if (WCOREDUMP(status)) {
      this->coreDump_ = true;
    }
#endif
  } else if (WIFSTOPPED(status)) {
    this->state_ = State::STOPPED;
    this->exitStatus_ = WSTOPSIG(status) + 128;
  } else if (WIFCONTINUED(status)) {
    this->state_ = State::RUNNING;
  }
  return true;
}

void Proc::showSignal() const {
  if (this->is(State::TERMINATED) && this->signaled()) {
    int sigNum = this->exitStatus_ - 128;
    fprintf(stderr, "%s%s\n", strsignal(sigNum), this->coreDump() ? " (core dumped)" : "");
    fflush(stderr); // FIXME: insert newline when terminated by CTRL-C or CTRL-Z
  }
}

int Proc::send(int sigNum) const {
  if (!this->is(State::TERMINATED)) {
    return kill(this->pid(), sigNum);
  }
  return 0;
}

// #######################
// ##     JobObject     ##
// #######################

static void formatPipeline(const StringRef ref, std::string &out) {
  splitByDelim(ref, '\0', [&out](StringRef sub, bool delim) {
    out += sub;
    if (delim) {
      out += " | ";
    }
    return true;
  });
}

static Proc::State resolvePipelineState(const JobObject &job) {
  unsigned int termCount = 0;
  unsigned int size = job.getProcSize();
  for (unsigned int i = 0; i < size; i++) {
    switch (job.getProcs()[i].state()) {
    case Proc::State::RUNNING:
      break;
    case Proc::State::STOPPED:
      return Proc::State::STOPPED;
    case Proc::State::TERMINATED:
      termCount++;
      break;
    }
  }
  return termCount == size ? Proc::State::TERMINATED : Proc::State::RUNNING;
}

static const char *toString(Proc::State state) {
  switch (state) {
  case Proc::State::RUNNING:
    return "Running";
  case Proc::State::STOPPED:
    return "Stopped";
  case Proc::State::TERMINATED:
    return "Done";
  }
  return ""; // normally unreachable, but suppress gcc warning
}

std::string JobObject::formatInfo(JobInfoFormat fmt) const {
  std::string value;
  if (hasFlag(fmt, JobInfoFormat::JOB_ID)) {
    value += "[";
    value += std::to_string(this->getJobID());
    value += "]";

    if (hasFlag(fmt, JobInfoFormat::CUR_JOB)) {
      value += " +";
    } else if (hasFlag(fmt, JobInfoFormat::PREV_JOB)) {
      value += " -";
    } else if (hasFlag(fmt, JobInfoFormat::OTHER_JOB)) {
      value += "  ";
    }
  }
  if (hasFlag(fmt, JobInfoFormat::PID)) {
    if (!value.empty()) {
      value += " ";
    }
    value += std::to_string(this->getProcs()[0].pid());
  }
  if (hasFlag(fmt, JobInfoFormat::STATE)) {
    if (!value.empty()) {
      value += " ";
    }
    value += toString(resolvePipelineState(*this));
  }
  if (hasFlag(fmt, JobInfoFormat::DESC)) {
    if (!value.empty()) {
      value += "  ";
    }
    formatPipeline(this->desc.asStrRef(), value);
  }
  value += "\n";
  return value;
}

bool JobObject::restoreStdin() {
  if (this->oldStdin > -1 && this->isControlled()) {
    dup2(this->oldStdin, STDIN_FILENO);
    close(this->oldStdin);
    this->oldStdin = -1;
    return true;
  }
  return false;
}

void JobObject::send(int sigNum) const {
  if (!this->available()) {
    return;
  }

  if (pid_t pid = this->getValidPid(0); pid > -1 && pid == getpgid(pid)) {
    kill(-pid, sigNum);
    return;
  }
  for (unsigned int i = 0; i < this->procSize; i++) {
    this->procs[i].send(sigNum);
  }
}

int JobObject::wait(WaitOp op) {
  if (!isControlled()) {
    errno = ECHILD;
    return -1;
  }

  errno = 0;
  int lastStatus = 0;
  if (this->available()) {
    for (unsigned short i = 0; i < this->procSize; i++) {
      auto &proc = this->procs[i];
      lastStatus = proc.wait(op, i == this->procSize - 1);
      if (lastStatus < 0) {
        return lastStatus;
      }
    }
    this->updateState();
  }
  if (!this->available()) {
    return this->exitStatus();
  }
  return lastStatus;
}

// #######################
// ##     ProcTable     ##
// #######################

struct PidEntryComp {
  bool operator()(const ProcTable::Entry &x, pid_t y) const { return x.pid() < y; }

  bool operator()(pid_t x, const ProcTable::Entry &y) const { return x < y.pid(); }
};

const ProcTable::Entry *ProcTable::addProc(pid_t pid, unsigned short jobId, unsigned short offset) {
  if (pid < 0 || jobId == 0) {
    return nullptr;
  }
  auto iter = std::lower_bound(this->entries.begin(), this->entries.end(), pid, PidEntryComp());
  if (iter == this->entries.end() || iter->pid() != pid) {
    auto ret = this->entries.insert(iter, ProcTable::Entry(pid, jobId, offset));
    return ret;
  }
  return nullptr;
}

ProcTable::Entry *ProcTable::findProc(pid_t pid) {
  if (pid > 0) {
    auto iter = std::lower_bound(this->entries.begin(), this->entries.end(), pid, PidEntryComp());
    if (iter != this->entries.end()) {
      return iter;
    }
  }
  return nullptr;
}

const ProcTable::Entry *ProcTable::findProc(pid_t pid) const {
  if (pid > 0) {
    auto iter = std::lower_bound(this->entries.begin(), this->entries.end(), pid, PidEntryComp());
    if (iter != this->entries.end()) {
      return iter;
    }
  }
  return nullptr;
}

void ProcTable::batchedRemove() {
  this->deletedCount = 0;
  unsigned int removedIndex;
  for (removedIndex = 0; removedIndex < this->entries.size(); removedIndex++) {
    if (this->entries[removedIndex].isDeleted()) {
      break;
    }
  }
  if (removedIndex == this->entries.size()) {
    return; // not found deleted entry
  }

  for (unsigned int i = removedIndex + 1; i < this->entries.size(); i++) {
    if (!this->entries[i].isDeleted()) {
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

Job JobTable::attach(Job job, bool disowned) {
  if (job->getJobID() == 0 && job->available()) { // not attached
    assert(!job->isDisowned());
    auto ret = this->findEmptyEntry();
    this->jobs.insert(this->jobs.begin() + ret, job);
    job->jobID = ret + 1;
    this->procTable.add(job);
    if (disowned) {
      job->disown();
    } else {
      this->setCurrentJob(job);
    }
  }
  return job;
}

int JobTable::waitForJob(const Job &job, WaitOp op) {
  LOG(DUMP_WAIT, "@@enter op: %s", toString(op));
  int status = this->waitForJobImpl(job, op);
  int e = errno;
  this->removeTerminatedJobs();
  errno = e;
  LOG(DUMP_WAIT, "@@exit");
  return status;
}

void JobTable::waitForAny() {
  SignalGuard guard;
  DSState::clearPendingSignal(SIGCHLD);

  for (WaitResult ret; (ret = waitForProc(-1, WaitOp::NONBLOCKING)).pid != 0;) {
    if (ret.pid == -1) {
      break;
    }
    this->updateProcState(ret);
  }
  this->removeTerminatedJobs();
}

const JobTable::CurPrevJobs &JobTable::syncAndGetCurPrevJobs() {
  if (auto &prev = this->curPrevJobs.prev; prev && prev->isDisowned()) {
    prev = nullptr;
  }
  if (auto &cur = this->curPrevJobs.cur; cur && cur->isDisowned()) {
    cur = std::move(this->curPrevJobs.prev);
    this->curPrevJobs.prev = nullptr;
  }

  if (!this->curPrevJobs.prev) {
    for (auto iter = this->jobs.rbegin(); iter != this->jobs.rend(); ++iter) {
      auto &j = *iter;
      if (j->is(JobObject::State::TERMINATED) || j->isDisowned()) {
        continue;
      }
      if (j != this->curPrevJobs.cur) {
        this->curPrevJobs.prev = j;
        if (!this->curPrevJobs.cur) {
          this->curPrevJobs.cur = std::move(this->curPrevJobs.prev);
          this->curPrevJobs.prev = nullptr;
        } else {
          break;
        }
      }
    }
  } else {
    assert(this->curPrevJobs.cur);
  }
  return this->curPrevJobs;
}

static const Proc *findLastStopped(const Job &job) {
  if (!job) {
    return nullptr;
  }
  const Proc *last = nullptr;
  for (unsigned int i = 0; i < job->getProcSize(); i++) {
    auto &p = job->getProcs()[i];
    if (p.is(Proc::State::RUNNING)) {
      return nullptr;
    } else if (p.is(Proc::State::STOPPED)) {
      last = &p;
    }
  }
  return last;
}

int JobTable::waitForJobImpl(const Job &job, WaitOp op) {
  if (job && !job->available()) {
    return job->wait(op);
  }
  if (const Proc * p; op == WaitOp::BLOCK_UNTRACED && (p = findLastStopped(job))) {
    return p->exitStatus();
  }

  int lastStatus = 0;
  for (WaitResult ret; (ret = waitForProc(-1, op)).pid != 0;) {
    if (ret.pid == -1) {
      return -1;
    }
    auto pair = this->updateProcState(ret);
    if (!job) {
      return 0;
    } else if (pair.first && pair.first == job) {
      auto &proc = job->getProcs()[pair.second];
      lastStatus = proc.exitStatus();
      if (const Proc * last; proc.is(Proc::State::STOPPED) && op == WaitOp::BLOCK_UNTRACED &&
                             (last = findLastStopped(job))) {
        return last->exitStatus();
      }
    }
    if (job->is(JobObject::State::TERMINATED) && !job->isLastPipe()) {
      job->getProcs()[job->getProcSize() - 1].showSignal();
      return job->exitStatus();
    }
  }
  return lastStatus;
}

unsigned int JobTable::findEmptyEntry() const {
  unsigned int firstIndex = 0;
  unsigned int dist = this->jobs.size();

  while (dist > 0) {
    unsigned int hafDist = dist / 2;
    unsigned int midIndex = hafDist + firstIndex;
    if (this->jobs[midIndex]->getJobID() == midIndex + 1) {
      firstIndex = midIndex + 1;
      dist = dist - hafDist - 1;
    } else {
      dist = hafDist;
    }
  }
  return firstIndex;
}

struct Comparator {
  bool operator()(const Job &x, unsigned int y) const { return x->getJobID() < y; }

  bool operator()(unsigned int x, const Job &y) const { return x < y->getJobID(); }
};

JobTable::ConstEntryIter JobTable::findIter(unsigned int jobId) const {
  if (jobId > 0) {
    auto iter = std::lower_bound(this->jobs.begin(), this->jobs.end(), jobId, Comparator());
    if (iter != this->jobs.end() && (*iter)->jobID == jobId) {
      return iter;
    }
  }
  return this->jobs.end();
}

std::pair<Job, unsigned int> JobTable::updateProcState(WaitResult ret) {
  if (ProcTable::Entry * entry; (entry = this->procTable.findProc(ret.pid))) {
    assert(!entry->isDeleted());
    auto iter = this->findIter(entry->jobId());
    assert(iter != this->jobs.end());
    auto &job = *iter;
    auto &proc = job->procs[entry->procOffset()];
    proc.updateState(ret);
    if (proc.is(Proc::State::TERMINATED)) {
      this->procTable.deleteProc(*entry);
    }
    job->updateState();
    return {job, entry->procOffset()};
  }
  return {nullptr, 0};
}

void JobTable::removeTerminatedJobs() {
  if (this->procTable.getDeletedCount() == 0) {
    return;
  }
  this->procTable.batchedRemove();

  unsigned int removedIndex;
  for (removedIndex = 0; removedIndex < this->jobs.size(); removedIndex++) {
    if (!this->jobs[removedIndex]->available()) {
      break;
    }
  }
  if (removedIndex == this->jobs.size()) {
    return; // not found deleted entry
  }

  for (unsigned int i = removedIndex + 1; i < this->jobs.size(); i++) {
    if (this->jobs[i]->available()) {
      assert(!this->jobs[removedIndex]->available());
      std::swap(this->jobs[i], this->jobs[removedIndex]);
      removedIndex++;
    }
  }
  assert(removedIndex < this->jobs.size());
  for (; this->jobs.size() != removedIndex; this->jobs.pop_back()) {
    auto &job = this->jobs.back();
    job->jobID = 0;
    job->disown();
  }

  // clear cur/prev job
  if (auto &prev = this->curPrevJobs.prev; prev && prev->is(JobObject::State::TERMINATED)) {
    prev = nullptr;
  }
  if (auto &cur = this->curPrevJobs.cur; cur && cur->is(JobObject::State::TERMINATED)) {
    cur = std::move(this->curPrevJobs.prev);
    this->curPrevJobs.prev = nullptr;
  }
}

} // namespace ydsh