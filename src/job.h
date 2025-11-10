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

#ifndef ARSH_JOB_H
#define ARSH_JOB_H

#include <type_traits>
#include <vector>

#include "object.h"

namespace arsh {

int beForeground(int ttyFd, pid_t pid);

#define EACH_WAIT_OP(OP)                                                                           \
  OP(BLOCKING)                                                                                     \
  OP(BLOCK_UNTRACED)                                                                               \
  OP(NONBLOCKING)

enum class WaitOp : unsigned char {
#define GEN_ENUM(OP) OP,
  EACH_WAIT_OP(GEN_ENUM)
#undef GEN_ENUM
};

#define EACH_WAIT_RESULT_KIND(OP)                                                                  \
  OP(EXITED)                                                                                       \
  OP(SIGNALED)                                                                                     \
  OP(STOPPED)                                                                                      \
  OP(CONTINUED)                                                                                    \
  OP(ERROR)

struct WaitResult {
  enum class Kind : unsigned char {
#define GEN_ENUM(OP) OP,
    EACH_WAIT_RESULT_KIND(GEN_ENUM)
#undef GEN_ENUM
  };

  static constexpr unsigned char SIGNALED_STATUS_OFFSET = 128;

  pid_t pid{-1};
  Kind kind{Kind::EXITED};
  unsigned char exitStatus{0};
  bool coreDump{false};
};

WaitResult waitForProc(pid_t pid, WaitOp op);

class Proc {
public:
  enum class State : unsigned char {
    RUNNING,
    STOPPED,    // stopped by SIGSTOP or SIGTSTP
    TERMINATED, // terminated
  };

  struct Param {
    pid_t pgid{0};          // for `setpgid`. if maybe 0
    bool jobControl{false}; // enable job control in a child process
    bool foreground{false}; // child process should be foreground-job
    bool sync{false};       // synchronize child process creation

    bool hasGroup() const { return this->jobControl && this->pgid == 0; }
  };

private:
  pid_t pid_{-1};
  State state_{State::TERMINATED};

  /**
   * enabled when `state' is TERMINATED.
   */
  unsigned char exitStatus_{0};

  static constexpr unsigned short ATTR_SIGNALED = 1u << 0u;
  static constexpr unsigned short ATTR_CORE_DUMP = 1u << 1u;
  static constexpr unsigned short ATTR_GROUP_LEADER = 1u << 2u; // pid == pgid

  unsigned short attr{0};

  explicit Proc(pid_t pid) : pid_(pid), state_(State::RUNNING) {}

public:
  Proc() = default;

  pid_t pid() const { return this->pid_; }

  State state() const { return this->state_; }

  bool is(State s) const { return this->state() == s; }

  int exitStatus() const { return this->exitStatus_; }

  bool signaled() const { return hasFlag(this->attr, ATTR_SIGNALED); }

  int asSigNum() const {
    assert(this->signaled());
    return this->exitStatus() - WaitResult::SIGNALED_STATUS_OFFSET;
  }

  bool coreDump() const { return hasFlag(this->attr, ATTR_CORE_DUMP); }

  bool groupLeader() const { return hasFlag(this->attr, ATTR_GROUP_LEADER); }

  /**
   * wait for termination
   * @param op
   * @param showSignal
   * if true, print the signal message when terminated by signal.
   * @return
   * if waitpid return 0, set 'exitStatus_' and return it.
   * if waitpid return -1, return -1.
   */
  int wait(WaitOp op, bool showSignal = true) {
    if (!this->is(State::TERMINATED)) {
      if (this->is(State::STOPPED)) {
        op = WaitOp::NONBLOCKING;
      }
      WaitResult ret = waitForProc(this->pid(), op);
      if (ret.pid > 0) {
        this->updateState(ret);
        if (showSignal) {
          this->showSignal();
        }
      } else if (ret.pid < 0) {
        return -1;
      }
    }
    return this->exitStatus_;
  }

  bool updateState(WaitResult ret);

  /**
   * show the signal message to stderr (for terminated by signal)
   */
  void showSignal() const;

  /**
   * send signal to proc
   * @param sigNum
   * @return
   * if success, return 0.
   */
  int send(int sigNum) const;

  /**
   * create a new child process.
   * after fork, reset user-defined signal handler setting in a child process.
   * @param st
   * @param param
   * job-control related parameter
   * @return
   * if Proc#pid() is -1, `fork` failed due to EAGAIN.
   */
  static Proc fork(ARState &st, Param param);
};

class ProcTable;

class JobTable;

enum class JobInfoFormat : unsigned short {
  JOB_ID = 1u << 0u,
  STATE = 1u << 1u,
  CUR_JOB = 1u << 2u,
  PREV_JOB = 1u << 3u,
  OTHER_JOB = 1u << 4u,
  PID = 1u << 5u,
  DESC = 1u << 6u,
  VERBOSE = 1u << 7u,

  DEFAULT = JOB_ID | STATE | CUR_JOB | DESC,
};

template <>
struct allow_enum_bitop<JobInfoFormat> : std::true_type {};

class JobObject : public ObjectWithRtti<ObjectKind::Job> {
public:
  static_assert(std::is_standard_layout_v<Proc>, "failed");

  enum class State : unsigned char {
    AVAILABLE,    // running or stopped job
    TERMINATED,   // already terminated
    UNCONTROLLED, // job is not created its own parent process
  };

private:
  /**
   * writable file descriptor (connected to STDIN of Job). must be UnixFD_Object
   */
  ObjPtr<UnixFdObject> inObj;

  /**
   * readable file descriptor (connected to STDOUT of Job). must be UnixFD_Object
   */
  ObjPtr<UnixFdObject> outObj;

  /**
   * if already closed, will be -1.
   */
  int oldStdin{-1};

  /**
   * after termination will be 0
   */
  unsigned short jobID{0};

  static constexpr unsigned char ATTR_DISOWNED = 1u << 7u;
  static constexpr unsigned char ATTR_LAST_PIPE = 1u << 6u;

  static constexpr unsigned char STATE_MASK = 0x0F;

  /**
   * upper 4bit is attribute
   * lower 4bit is job state
   */
  unsigned char meta{static_cast<unsigned char>(State::AVAILABLE)};

  unsigned char procSize;

  Value desc; // for `jobs` command output. must be String

  /**
   * initial size is procSize
   */
  Proc procs[];

  friend class JobTable;

  NON_COPYABLE(JobObject);

  JobObject(unsigned int size, const Proc *procs, bool saveStdin, ObjPtr<UnixFdObject> inObj,
            ObjPtr<UnixFdObject> outObj, Value &&desc);

public:
  static ObjPtr<JobObject> create(const unsigned int size, const Proc *procs, const bool saveStdin,
                                  ObjPtr<UnixFdObject> inObj, ObjPtr<UnixFdObject> outObj,
                                  Value &&desc) {
    void *ptr = operator new(sizeof(JobObject) + (sizeof(Proc) * size));
    auto *entry = new (ptr)
        JobObject(size, procs, saveStdin, std::move(inObj), std::move(outObj), std::move(desc));
    return ObjPtr<JobObject>(entry);
  }

  static ObjPtr<JobObject> fromPipe(const unsigned int size, const Proc *procs, Value &&desc) {
    return create(size, procs, false, UnixFdObject::empty(), UnixFdObject::empty(),
                  std::move(desc));
  }

  static ObjPtr<JobObject> fromLastPipe(const unsigned int size, const Proc *procs, Value &&desc) {
    return create(size, procs, true, UnixFdObject::empty(), UnixFdObject::empty(), std::move(desc));
  }

  static ObjPtr<JobObject> fromProc(Proc proc, Value &&desc) {
    const Proc procs[1] = {proc};
    return fromPipe(1, procs, std::move(desc));
  }

  void operator delete(void *ptr) { ::operator delete(ptr); }

  unsigned int getProcSize() const { return this->procSize; }

  State state() const { return static_cast<State>(STATE_MASK & this->meta); }

  bool is(State st) const { return this->state() == st; }

  bool isAvailable() const { return this->is(State::AVAILABLE); }

  bool isTerminated() const { return this->is(State::TERMINATED); }

  bool isDisowned() const { return hasFlag(this->meta, ATTR_DISOWNED); }

  void disown() { setFlag(this->meta, ATTR_DISOWNED); }

  bool isLastPipe() const { return hasFlag(this->meta, ATTR_LAST_PIPE); }

  bool isGrouped() const { return this->procs[0].groupLeader(); }

  const Proc *getProcs() const { return this->procs; }

  const Proc &lastProc() const { return this->getProcs()[this->getProcSize() - 1]; }

  /**
   * get valid pid
   * @param index
   * @return
   * after termination (or uncontrolled), return -1.
   */
  pid_t getValidPid(unsigned int index) const {
    if (this->isControlled()) {
      auto &proc = this->procs[index];
      if (!proc.is(Proc::State::TERMINATED)) {
        return proc.pid();
      }
    }
    return -1;
  }

  pid_t getPGID() const {
    if (this->isAvailable() && this->isGrouped()) {
      return this->procs[0].pid(); // return valid pgid if grouped
    }
    return -1;
  }

  /**
   *
   * @return
   * after terminated, return 0.
   */
  unsigned short getJobID() const { return this->jobID; }

  bool isControlled() const { return !this->is(State::UNCONTROLLED); }

  Value getInObj() const { return this->inObj; }

  Value getOutObj() const { return this->outObj; }

  std::string formatInfo(JobInfoFormat fmt) const;

  void showInfo(FILE *fp = stderr, JobInfoFormat fmt = JobInfoFormat::DEFAULT) const {
    auto value = this->formatInfo(fmt);
    fputs(value.c_str(), fp);
    fflush(fp);
  }

  /**
   * restore STDIN_FD
   * if no ownership, do nothing.
   * @return
   * if restore fd, return true.
   * if already called, return false
   */
  bool restoreStdin();

  /**
   * send signal to all processes.
   * if jos is a process group leader, send a signal to a process group
   * @param sigNum
   * @return
   * if failed, return false and set errno
   */
  bool send(int sigNum) const;

  /**
   * the process group of this job will be the foreground process group
   * @param ttyFd
   * @return
   * if success, return 0.
   * if do-nothing, return 1.
   * if error, return -1 and set errno
   */
  int tryToBeForeground(int ttyFd) const;

  void updateState() {
    if (this->isAvailable()) {
      unsigned int c = 0;
      for (unsigned int i = 0; i < this->getProcSize(); i++) {
        if (this->getProcs()[i].is(Proc::State::TERMINATED)) {
          c++;
        }
      }
      if (c == this->getProcSize()) {
        this->setState(State::TERMINATED);
      }
    }
  }

  /**
   * get exit status of the last process
   * @return
   */
  int exitStatus() const { return this->procs[this->procSize - 1].exitStatus(); }

  /**
   * wait for termination.
   * after termination, `state` will be `TERMINATED`.
   * @param op
   * @return
   * exit status of the last process.
   * if termination failed (has no-ownership or has error), return -1 and set errno
   */
  int wait(WaitOp op);

private:
  void setState(State s) {
    unsigned char attr = ~STATE_MASK & this->meta;
    this->meta = static_cast<unsigned char>(s) | attr;
  }
};

using Job = ObjPtr<JobObject>;

// for pid to job mapping
class ProcTable {
public:
  class Entry {
  private:
    friend class ProcTable;

    pid_t pid_;
    unsigned short jobId_;
    unsigned short procOffset_;

  public:
    Entry() = default;

    Entry(pid_t pid, unsigned short jobId, unsigned short offset)
        : pid_(pid), jobId_(jobId), procOffset_(offset) {}

    pid_t pid() const { return this->pid_; }

    unsigned short jobId() const { return this->jobId_; }

    unsigned short procOffset() const { return this->procOffset_; }

    bool isDeleted() const { return this->jobId() == 0; }
  };

private:
  FlexBuffer<Entry> entries;
  unsigned int deletedCount{0};
  const int ttyFd; // never closed

public:
  explicit ProcTable(int ttyFd) : ttyFd(ttyFd) {}

  int getTTYFd() const { return this->ttyFd; }

  const FlexBuffer<Entry> &getEntries() const { return this->entries; }

  void add(const Job &job) {
    for (unsigned int i = 0; i < job->getProcSize(); i++) {
      this->addProc(job->getValidPid(i), job->getJobID(), i);
    }
  }

  const Entry *addProc(pid_t pid, unsigned short jobId, unsigned short offset);

  Entry *findProc(pid_t pid);

  const Entry *findProc(pid_t pid) const;

  /**
   * call deleteProc() in a specified entry by pid.
   * the actual delete operation is not performed until calling batchedRemove()
   * @param pid
   * @return
   * if found corresponding entry, return true
   */
  bool deleteProc(pid_t pid) {
    auto *e = this->findProc(pid);
    if (e) {
      this->deleteProc(*e);
    }
    return e != nullptr;
  }

  void deleteProc(ProcTable::Entry &e) {
    if (!e.isDeleted()) {
      e.jobId_ = 0;
      this->deletedCount++;
    }
  }

  void clear() {
    this->entries.clear();
    this->deletedCount = 0;
  }

  unsigned int viableProcSize() const { return this->entries.size() - this->deletedCount; }

  unsigned int getDeletedCount() const { return this->deletedCount; }

  /**
   * remove all deleted PidEntry
   */
  void batchedRemove();
};

class JobNotifyCallback {
private:
  std::vector<std::string> values;

public:
  void add(std::string &&v) { this->values.push_back(std::move(v)); }

  void showAndClear() {
    for (auto &e : this->values) {
      fputs(e.c_str(), stderr);
      fflush(stderr);
    }
    this->values.clear();
  }
};

class JobLookupResult {
public:
  enum class ErrorType : unsigned char {
    NO_JOB,  // valid job-spec format, but no such job
    NO_PROC, // valid pid format, but no such process
    INVALID, // invalid job-spec or pid format
  };

  struct Error {
    ErrorType type;
    int value;
  };

private:
  Union<Job, const ProcTable::Entry *, Error> value;

public:
  explicit JobLookupResult(Job &&job) : value(std::move(job)) {}

  explicit JobLookupResult(const ProcTable::Entry *entry) : value(entry) {}

  explicit JobLookupResult(Error e) : value(e) {}

  bool isJob() const { return is<Job>(this->value); }

  bool isProc() const { return is<const ProcTable::Entry *>(this->value); }

  bool isError() const { return is<Error>(this->value); }

  const Job &asJob() const { return get<Job>(this->value); }

  const ProcTable::Entry &asProc() const { return *get<const ProcTable::Entry *>(this->value); }

  Error asError() const { return get<Error>(this->value); }
};

class JobTable {
public:
  struct CurPrevJobs {
    Job cur; // latest attached entry
    Job prev;

    JobInfoFormat getJobType(const Job &job) const {
      if (this->cur == job) {
        return JobInfoFormat::CUR_JOB;
      } else if (this->prev == job) {
        return JobInfoFormat::PREV_JOB;
      } else {
        return JobInfoFormat::OTHER_JOB;
      }
    }
  };

private:
  std::vector<Job> jobs;

  ProcTable procTable;

  CurPrevJobs curPrevJobs;

  Job toplevelLastPipeJob; // if not null, within last-pipe

  ObserverPtr<JobNotifyCallback> notifyCallback;

public:
  NON_COPYABLE(JobTable);

  using ConstEntryIter = std::vector<Job>::const_iterator;

  explicit JobTable(int ttyFd) : procTable(ttyFd) {}
  ~JobTable() = default;

  void setNotifyCallback(ObserverPtr<JobNotifyCallback> callback) {
    this->notifyCallback = callback;
  }

  Job attach(Job job, bool disowned = false);

  /**
   * for subshell
   * job-table still maintains jobs but not control theme
   */
  void loseControlOfJobs() {
    for (auto &e : this->jobs) {
      e->setState(JobObject::State::UNCONTROLLED);
    }
    this->procTable.clear();
    this->toplevelLastPipeJob = nullptr;
  }

  /**
   * if no ownership, wait for termination.
   * @param job
   * maybe null. if null, return immediately
   * @param op
   * @param suppressNotify
   * if true, suppress notification of specified job termination
   * @return
   * exit status of the last process.
   * after waiting termination, remove entry.
   */
  int waitForJob(const Job &job, WaitOp op, bool suppressNotify = false);

  /**
   * update the status of managed jobs.
   * when a job is terminated, detach a job.
   * should call after wait termination of a foreground job.
   */
  void waitForAny();

  /**
   *
   * @param job
   * must be already attached job (still available and owned)
   */
  void setCurrentJob(Job job) {
    if (job->getJobID() != 0 && job->isAvailable() && !job->isDisowned() && !job->isLastPipe() &&
        this->curPrevJobs.cur != job) {
      if (auto &cur = this->curPrevJobs.cur; cur && cur->isDisowned()) {
        cur = nullptr;
      }
      this->curPrevJobs.prev = std::move(this->curPrevJobs.cur);
      this->curPrevJobs.cur = std::move(job);
    }
  }

  /**
   * sync and get the cur/prev job
   * @return
   */
  const CurPrevJobs &syncAndGetCurPrevJobs();

  void removeToplevelLastPipeJobIfSame(const Job &job) {
    if (this->toplevelLastPipeJob == job) {
      this->toplevelLastPipeJob = nullptr;
    }
  }

  Job getToplevelLastPipeJob() const { return this->toplevelLastPipeJob; }

  pid_t getToplevelLastPipePGID() const {
    if (this->toplevelLastPipeJob) {
      return this->toplevelLastPipeJob->getPGID();
    }
    return -1;
  }

  /**
   * get Job by job id
   * @param jobId
   * @return
   * if not found, return nullptr
   * if the job is disowned, return nullptr
   */
  Job find(unsigned int jobId) const {
    auto iter = this->findIter(jobId);
    if (iter == this->end() || (*iter)->isDisowned()) {
      return nullptr;
    }
    return *iter;
  }

  /**
   * send signal to all jobs except for disowned jobs
   * @param sigNum
   */
  void send(int sigNum) const {
    for (auto &job : this->jobs) {
      if (!job->isDisowned()) {
        static_cast<void>(job->send(sigNum));
      }
    }
  }

  unsigned int size() const { return this->jobs.size(); }

  // helper method for entry lookup
  ConstEntryIter begin() const { return this->jobs.begin(); }

  ConstEntryIter end() const { return this->jobs.end(); }

  const ProcTable &getProcTable() const { return this->procTable; }

  /**
   * @param key
   * must be job-spec or pid
   * @param allowProc
   * if true, also lookup process
   * @return
   */
  JobLookupResult lookup(StringRef key, bool allowProc = false);

private:
  /**
   *
   * @return
   * entry index.
   * new job id is index + 1
   */
  unsigned int findEmptyEntry() const;

  /**
   *
   * @param jobId
   * greater than 0.
   * @return
   * if not found, return end
   */
  ConstEntryIter findIter(unsigned int jobId) const;

  /**
   *
   * @param ret
   * @return
   * return corresponding proc
   */
  std::pair<Job, unsigned int> updateProcState(WaitResult ret);

  void notifyTermination(const Job &job);

  void removeTerminatedJobs();
};

/**
 * for JobObject
 * @param ref
 * @param out
 * append formatted string
 * @return
 * if out.size() reaches the limit, trim out and return false
 */
bool formatJobDesc(StringRef ref, std::string &out);

} // namespace arsh

#endif // ARSH_JOB_H
