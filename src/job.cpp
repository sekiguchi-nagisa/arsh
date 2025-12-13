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

namespace arsh {

static int changeForegroundProcessGroup(int ttyFd, pid_t pgid) {
  errno = 0;
  const int r = tcsetpgrp(ttyFd, pgid);
  const int old = errno;
  LOG(DUMP_TCSETPGRP, "tcsetpgrp(%d, %d) = (%d, %s)", ttyFd, pgid, r, strerror(old));
  errno = old;
  return r;
}

int beForeground(int ttyFd, pid_t pid) { return changeForegroundProcessGroup(ttyFd, getpgid(pid)); }

Proc Proc::fork(ARState &st, const Param param) {
  SignalGuard guard;

  // flush standard stream due to prevent mixing io buffer
  flushStdFD();

  if (st.jobTable.size() >= SYS_LIMIT_JOB_TABLE_SIZE ||
      st.subshellLevel() >= static_cast<int>(SYS_LIMIT_SUBSHELL_LEVEL)) {
    errno = EAGAIN;
    return Proc(-1);
  }

  Pipe selfPipe;
  if (param.sync && !selfPipe.open()) {
    return Proc(-1);
  }
  const auto childRng = st.getRng().split();
  pid_t pid = ::fork();
  if (pid == 0) { // child process
    if (param.jobControl) {
      setpgid(0, param.pgid);
      if (param.foreground) {
        beForeground(st.getTTYFd(), 0);
      }
      setJobControlSignalSetting(st, false);
    }

    // reset signal setting
    ARState::clearPendingSignal();
    st.canHandleSignal = true;
    resetSignalSettingUnblock(st);

    // clear JobTable entries
    st.jobTable.loseControlOfJobs();
    st.jobTable.setNotifyCallback(nullptr);

    // clear termination hook
    assert(st.termHookIndex != 0);
    st.setGlobal(st.termHookIndex, Value::createInvalid());

    // update PID
    st.setGlobal(BuiltinVarOffset::PID, Value::createInt(getpid()));

    // no inherit parent process RNG (due to prevent duplicated random numbers)
    st.getRng() = childRng;

    st.incSubShellLevel();

    if (param.sync) {
      selfPipe.close(READ_PIPE);
      if (write(selfPipe[WRITE_PIPE], "1", 1) != 1) {
      } // ignore error
    }
  } else if (pid > 0) {
    if (param.jobControl) {
      setpgid(pid, param.pgid);
      if (param.foreground) {
        beForeground(st.getTTYFd(), pid);
      }
    }
    if (param.sync) {
      /**
       * wait for child process setup completion due to rance condition of `tcsetpgrp`
       *
       * ex. cmd0 & cmd1 | cmd2 | { fg; }
       * Ideally `cmd1` will be foreground process and after that, `fg` will change `cmd0` with
       * foreground. In some slow execution environment, `cmd1` has not be foreground before call
       * `fg`. `fg` change `cmd0` with foreground, but `cmd1` will be foreground (race condition).
       * As a result, `cmd0` still be background
       */
      selfPipe.close(WRITE_PIPE);
      char b[1];
      if (read(selfPipe[READ_PIPE], b, 1) != 1) {
      } // ignore error
    }
  }
  selfPipe.close();
  Proc proc(pid);
  if (pid > 0 && param.hasGroup()) {
    setFlag(proc.attr, ATTR_GROUP_LEADER);
  }
  return proc;
}

static void setWaitResult(int status, WaitResult &ret) {
  if (WIFEXITED(status)) {
    ret.kind = WaitResult::Kind::EXITED;
    ret.exitStatus = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    ret.kind = WaitResult::Kind::SIGNALED;
    ret.exitStatus = WTERMSIG(status) + WaitResult::SIGNALED_STATUS_OFFSET;
#ifdef WCOREDUMP
    if (WCOREDUMP(status)) {
      ret.coreDump = true;
    }
#endif
  } else if (WIFSTOPPED(status)) {
    ret.kind = WaitResult::Kind::STOPPED;
    ret.exitStatus = WSTOPSIG(status) + WaitResult::SIGNALED_STATUS_OFFSET;
  } else if (WIFCONTINUED(status)) {
    ret.kind = WaitResult::Kind::CONTINUED;
  }
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

static std::string toString(const WaitResult ret) {
  constexpr const char *kinds[] = {
#define GEN_STR(OP) #OP,
      EACH_WAIT_RESULT_KIND(GEN_STR)
#undef GEN_STR
  };

  std::string value = "pid=";
  value += std::to_string(ret.pid);
  value += ',';
  if (ret.kind == WaitResult::Kind::ERROR) {
    value += " errno=";
    value += strerror(ret.exitStatus);
  } else {
    char data[64];
    snprintf(data, std::size(data), " kind=%s, exitStatus=%d, coredump=%u",
             kinds[toUnderlying(ret.kind)], static_cast<unsigned int>(ret.exitStatus),
             ret.coreDump ? 1 : 0);
    value += data;
  }
  return value;
}

// #endif

WaitResult waitForProc(const pid_t pid, const WaitOp op) {
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

  LOG(DUMP_WAIT, "call waitpid(%d, %s)", pid, toString(op));

  int status = 0;
  errno = 0;
  WaitResult ret;
  ret.pid = waitpid(pid, &status, option);
  const int errNum = errno;
  if (ret.pid < 0) {
    ret.kind = WaitResult::Kind::ERROR;
    ret.exitStatus = errNum;
  } else {
    setWaitResult(status, ret);
  }

  // dump waitpid status
  LOG(DUMP_WAIT, "return waitpid(%d, %s) = (%s)", pid, toString(op), toString(ret).c_str());

  errno = errNum; // NOLINT
  return ret;
}

// ##################
// ##     Proc     ##
// ##################

bool Proc::updateState(WaitResult ret) {
  assert(ret.pid == this->pid());
  if (this->is(State::TERMINATED)) {
    return false;
  }

  switch (ret.kind) {
  case WaitResult::Kind::EXITED:
    this->state_ = State::TERMINATED;
    this->exitStatus_ = ret.exitStatus;
    break;
  case WaitResult::Kind::SIGNALED:
    this->state_ = State::TERMINATED;
    this->exitStatus_ = ret.exitStatus;
    setFlag(this->attr, ATTR_SIGNALED);
    if (ret.coreDump) {
      setFlag(this->attr, ATTR_CORE_DUMP);
    }
    break;
  case WaitResult::Kind::STOPPED:
    this->state_ = State::STOPPED;
    this->exitStatus_ = ret.exitStatus;
    break;
  case WaitResult::Kind::CONTINUED:
    this->state_ = State::RUNNING;
    break;
  case WaitResult::Kind::ERROR:
    break; // unreachable
  }
  return true;
}

void Proc::showSignal() const {
  if (this->is(State::TERMINATED) && this->signaled()) {
    int sigNum = this->asSigNum();
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

JobObject::JobObject(unsigned int size, const Proc *procs, const bool saveStdin,
                     ObjPtr<UnixFdObject> inObj, ObjPtr<UnixFdObject> outObj, Value &&desc)
    : ObjectWithRtti(TYPE::Job), inObj(std::move(inObj)), outObj(std::move(outObj)), procSize(size),
      desc(std::move(desc)) {
  assert(size <= UINT8_MAX);
  for (unsigned int i = 0; i < this->procSize; i++) {
    this->procs[i] = procs[i];
  }
  if (saveStdin) {
    this->oldStdin = dupFDCloseOnExec(STDIN_FILENO); // FIXME: report error
    setFlag(this->meta, ATTR_LAST_PIPE);
  }
}

static void formatPipeline(const StringRef ref, std::string &out) {
  splitByDelim(ref, '\0', [&out](StringRef sub, bool delim) {
    out += sub;
    if (delim) {
      out += " | ";
    }
    return true;
  });
}

static std::vector<std::string> splitPipeline(const StringRef ref) {
  std::vector<std::string> values;
  splitByDelim(ref, '\0', [&values](StringRef sub, bool delim) {
    std::string value = sub.toString();
    if (delim) {
      value += " |";
    }
    values.push_back(std::move(value));
    return true;
  });
  return values;
}

static void formatProcState(const Proc &proc, std::string &out) {
  if (!out.empty()) {
    out += " ";
  }
  switch (proc.state()) {
  case Proc::State::RUNNING:
    out += "Running";
    break;
  case Proc::State::STOPPED:
    out += "Stopped";
    break;
  case Proc::State::TERMINATED:
    if (proc.signaled()) {
      int sigNum = proc.asSigNum();
      out += strsignal(sigNum);
    } else if (int s = proc.exitStatus(); s != 0) {
      out += "Exit ";
      out += std::to_string(s);
    } else {
      out += "Done";
    }
    break;
  }
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

  // format remain
  if (!hasFlag(fmt, JobInfoFormat::VERBOSE)) {
    if (hasFlag(fmt, JobInfoFormat::PID)) {
      if (!value.empty()) {
        value += " ";
      }
      value += std::to_string(this->getProcs()[0].pid());
    }
    if (hasFlag(fmt, JobInfoFormat::STATE)) {
      formatProcState(this->lastProc(), value);
    }
    if (hasFlag(fmt, JobInfoFormat::DESC)) {
      if (!value.empty()) {
        value += "  ";
      }
      formatPipeline(this->desc.asStrRef(), value);
    }
    value += "\n";
  } else {
    unsigned int prefixLen = value.size();
    size_t pidLen = 0;
    size_t stateLen = 0;
    std::vector<std::pair<std::string, std::string>> fragments;
    for (unsigned int i = 0; i < this->procSize; i++) {
      auto &proc = this->getProcs()[i];
      auto pid = std::to_string(proc.pid());
      std::string state;
      formatProcState(proc, state);
      pidLen = std::max(pidLen, pid.size());
      stateLen = std::max(stateLen, state.size());
      fragments.emplace_back(std::move(pid), std::move(state));
    }
    auto descs = splitPipeline(this->desc.asStrRef());
    assert(descs.size() >= this->procSize);
    for (unsigned int i = 0; i < this->procSize; i++) {
      if (i > 0) {
        value.append(prefixLen, ' ');
      }
      auto &[pid, state] = fragments[i];
      formatTo(value, " %-*s %-*s  %s\n", static_cast<int>(pidLen), pid.c_str(),
               static_cast<int>(stateLen), state.c_str(), descs[i].c_str());
    }
  }
  return value;
}

bool JobObject::restoreStdin() {
  if (this->oldStdin > -1 && this->isControlled()) {
    dup2(this->oldStdin, STDIN_FILENO); // FIXME: report error
    close(this->oldStdin);
    this->oldStdin = -1;
    return true;
  }
  return false;
}

int JobObject::tryToBeForeground(int ttyFd) const {
  if (pid_t pgid = this->getPGID(); pgid > -1) {
    return changeForegroundProcessGroup(ttyFd, pgid);
  }
  return 1;
}

bool JobObject::send(int sigNum) const {
  if (!this->isAvailable()) {
    errno = ESRCH;
    return false;
  }
  if (pid_t pgid = this->getPGID(); pgid > -1) {
    return kill(-pgid, sigNum) == 0;
  }
  for (unsigned int i = 0; i < this->procSize; i++) {
    if (this->procs[i].send(sigNum) != 0) {
      return false;
    }
  }
  return true;
}

int JobObject::wait(WaitOp op) {
  if (!isControlled()) {
    errno = ECHILD;
    return -1;
  }

  errno = 0;
  int lastStatus = 0;
  if (this->isAvailable()) {
    for (unsigned short i = 0; i < this->procSize; i++) {
      auto &proc = this->procs[i];
      lastStatus = proc.wait(op, i == this->procSize - 1);
      if (lastStatus < 0) {
        return lastStatus;
      }
    }
    this->updateState();
  }
  if (!this->isAvailable()) {
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
  if (job->getJobID() == 0 && job->isAvailable()) { // not attached
    assert(!job->isDisowned());
    auto ret = this->findEmptyEntry();
    this->jobs.insert(this->jobs.begin() + ret, job);
    job->jobID = ret + 1;
    this->procTable.add(job);
    if (disowned) {
      job->disown();
    } else {
      this->setCurrentJob(job);
      if (!this->toplevelLastPipeJob && job->isLastPipe() && job->isGrouped()) {
        this->toplevelLastPipeJob = job;
      }
    }
  }
  return job;
}

void JobTable::waitForAny() {
  SignalGuard guard;
  ARState::clearPendingSignal(SIGCHLD);

  for (WaitResult ret; (ret = waitForProc(-1, WaitOp::NONBLOCKING)).pid != 0;) {
    if (ret.pid == -1) {
      break;
    }
    if (auto [job, offset] = this->updateProcState(ret); job && job->isTerminated()) {
      static_cast<void>(offset);
      this->notifyTermination(job);
    }
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
      if (j->isTerminated() || j->isDisowned() || j->isLastPipe()) {
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

int JobTable::waitForJob(const Job &job, WaitOp op, bool suppressNotify) {
  LOG(TRACE_JOB, "@@enter op: %s", toString(op));
  if (job && !job->isAvailable()) {
    return job->wait(op);
  }
  if (op == WaitOp::BLOCK_UNTRACED) {
    if (auto *p = findLastStopped(job)) {
      return p->exitStatus();
    }
  }

  auto cleanup = finally([&] {
    int old = errno;
    this->removeTerminatedJobs();
    errno = old;
  });

  int lastStatus = 0;
  for (WaitResult ret; (ret = waitForProc(-1, op)).pid != 0;) {
    if (ret.pid == -1) {
      return -1;
    }
    auto [j, offset] = this->updateProcState(ret);
    if (j && j->isTerminated()) {
      if (j != job || !suppressNotify) {
        this->notifyTermination(j);
      }
    }
    if (!job) {
      return 0;
    } else if (j == job) {
      auto &proc = job->getProcs()[offset];
      lastStatus = proc.exitStatus();
      if (proc.is(Proc::State::STOPPED) && op == WaitOp::BLOCK_UNTRACED) {
        if (auto *last = findLastStopped(job)) {
          return last->exitStatus();
        }
      }
    }
    if (job->isTerminated()) {
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
  if (ProcTable::Entry *entry = this->procTable.findProc(ret.pid)) {
    assert(!entry->isDeleted());
    auto iter = this->findIter(entry->jobId());
    assert(iter != this->jobs.end());
    auto &job = *iter;
    auto &proc = job->procs[entry->procOffset()];
    proc.updateState(ret);
    if (proc.is(Proc::State::TERMINATED)) {
      this->procTable.deleteProc(*entry);
      if (this->notifyCallback && this->procTable.getDeletedCount() == 1) {
        this->syncAndGetCurPrevJobs();
      }
    }
    job->updateState();
    return {job, entry->procOffset()};
  }
  return {nullptr, 0};
}

void JobTable::notifyTermination(const Job &job) {
  assert(job);
  if (!this->notifyCallback || !job->isTerminated() || job->isDisowned() || job->isLastPipe()) {
    return;
  }
  auto fmt =
      JobInfoFormat::JOB_ID | JobInfoFormat::STATE | JobInfoFormat::DESC | JobInfoFormat::VERBOSE;
  setFlag(fmt, this->curPrevJobs.getJobType(job));
  auto info = job->formatInfo(fmt);
  this->notifyCallback->add(std::move(info));
}

void JobTable::removeTerminatedJobs() {
  if (this->procTable.getDeletedCount() == 0) {
    return;
  }
  this->procTable.batchedRemove();

  unsigned int removedIndex;
  for (removedIndex = 0; removedIndex < this->jobs.size(); removedIndex++) {
    if (!this->jobs[removedIndex]->isAvailable()) {
      break;
    }
  }
  if (removedIndex == this->jobs.size()) {
    return; // not found deleted entry
  }

  for (unsigned int i = removedIndex + 1; i < this->jobs.size(); i++) {
    if (this->jobs[i]->isAvailable()) {
      assert(!this->jobs[removedIndex]->isAvailable());
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
  if (auto &prev = this->curPrevJobs.prev; prev && prev->isTerminated()) {
    prev = nullptr;
  }
  if (auto &cur = this->curPrevJobs.cur; cur && cur->isTerminated()) {
    cur = std::move(this->curPrevJobs.prev);
    this->curPrevJobs.prev = nullptr;
  }

  LOG(TRACE_JOB, "toplevelLastPipeJob, state=%d",
      this->toplevelLastPipeJob ? toUnderlying(this->toplevelLastPipeJob->state()) : -1);
  if (this->toplevelLastPipeJob && this->toplevelLastPipeJob->isTerminated()) {
    assert(this->toplevelLastPipeJob->isGrouped());
    LOG(TRACE_JOB, "remove toplevelLastPipeJob and switch back to foreground");
    beForeground(this->getProcTable().getTTYFd(), 0);
    this->toplevelLastPipeJob = nullptr;
  }
}

static auto toInt32(StringRef ref) { return convertToNum10<int32_t>(ref.begin(), ref.end()); }

JobLookupResult JobTable::lookup(StringRef key, bool allowProc) {
  if (key.empty()) {
    goto INVALID;
  }
  if (key[0] == '%') { // maybe job-spec
    key.removePrefix(1);
    if (key.size() == 1) {
      if (const char ch = key[0]; ch == '%' || ch == '+' || ch == '-') {
        auto &e = this->syncAndGetCurPrevJobs();
        if (ch == '-') { // prev
          if (e.prev) {
            return JobLookupResult(Job(e.prev));
          }
        } else if (e.cur) {
          return JobLookupResult(Job(e.cur));
        }
        return JobLookupResult({.type = JobLookupResult::ErrorType::NO_JOB, .value = 0});
      }
    }
    const auto ret = toInt32(key);
    if (!ret) {
      goto INVALID;
    }
    if (auto job = this->find(static_cast<unsigned int>(ret.value))) {
      return JobLookupResult(std::move(job));
    }
    return JobLookupResult({.type = JobLookupResult::ErrorType::NO_JOB, .value = ret.value});
  }
  if (allowProc && isDecimal(key[0])) { // may be pid
    const auto ret = toInt32(key);
    if (!ret || ret.value < 0) {
      goto INVALID;
    }
    if (auto *entry = this->getProcTable().findProc(static_cast<pid_t>(ret.value))) {
      return JobLookupResult(entry);
    }
    return JobLookupResult({.type = JobLookupResult::ErrorType::NO_PROC, .value = ret.value});
  }

INVALID:
  return JobLookupResult({JobLookupResult::ErrorType::INVALID, 0});
}

bool formatJobDesc(const StringRef ref, std::string &out) {
  return splitByDelim(ref, '\n', [&out](StringRef sub, bool newline) {
    out += sub;
    if (newline) {
      out += "\\n";
    }
    if (out.size() > SYS_LIMIT_JOB_DESC_LEN) {
      out.resize(SYS_LIMIT_JOB_DESC_LEN - 3);
      out += "...";
      return false;
    }
    return true;
  });
}

} // namespace arsh