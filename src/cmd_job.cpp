/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "cmd.h"
#include "misc/num_util.hpp"
#include "vm.h"

namespace ydsh {

// job control related builtin commands

static auto toInt32(StringRef str) { return convertToDecimal<int32_t>(str.begin(), str.end()); }

static int toSigNum(StringRef str) {
  if (!str.empty() && isDecimal(*str.data())) {
    auto pair = toInt32(str);
    if (!pair) {
      return -1;
    }
    auto sigList = getUniqueSignalList();
    return std::binary_search(sigList.begin(), sigList.end(), pair.value) ? pair.value : -1;
  }
  return getSignalNum(str);
}

static bool printNumOrName(StringRef str) {
  if (!str.empty() && isDecimal(*str.data())) {
    auto pair = toInt32(str);
    if (!pair) {
      return false;
    }
    const char *name = getSignalName(pair.value);
    if (name == nullptr) {
      return false;
    }
    printf("%s\n", name);
  } else {
    int sigNum = getSignalNum(str);
    if (sigNum < 0) {
      return false;
    }
    printf("%d\n", sigNum);
  }
  fflush(stdout);
  return true;
}

using ProcOrJob = Union<pid_t, Job, const ProcTable::Entry *>;

static ProcOrJob parseProcOrJob(const JobTable &jobTable, const ArrayObject &argvObj, StringRef arg,
                                bool allowNoChild) {
  bool isJob = arg.startsWith("%");
  auto pair = toInt32(isJob ? arg.substr(1) : arg);
  if (!pair) {
    ERROR(argvObj, "%s: arguments must be pid or job id", toPrintable(arg).c_str());
    return {};
  }
  int id = pair.value;

  if (isJob) {
    if (id > 0) {
      auto job = jobTable.find(static_cast<unsigned int>(id));
      if (job) {
        return {std::move(job)};
      }
    }
    ERROR(argvObj, "%s: no such job", toPrintable(arg).c_str());
    return {};
  } else {
    if (const ProcTable::Entry * e; id > -1 && (e = jobTable.getProcTable().findProc(id))) {
      return {e};
    } else if (allowNoChild) {
      return {id};
    } else {
      ERROR(argvObj, "%s: not a child of this shell", toPrintable(arg).c_str());
      return {};
    }
  }
}

static bool killProcOrJob(const JobTable &jobTable, const ArrayObject &argvObj, StringRef arg,
                          int sigNum) {
  auto target = parseProcOrJob(jobTable, argvObj, arg, true);
  if (!target.hasValue()) {
    return false;
  }
  if (is<pid_t>(target) || is<const ProcTable::Entry *>(target)) {
    pid_t pid =
        is<pid_t>(target) ? get<pid_t>(target) : get<const ProcTable::Entry *>(target)->pid();
    if (kill(pid, sigNum) < 0) {
      PERROR(argvObj, "%s", toPrintable(arg).c_str());
      return false;
    }
  } else if (is<Job>(target)) {
    get<Job>(target)->send(sigNum);
  } else {
    return false;
  }
  return true;
}

// -s sig (pid | jobspec ...)
// -l
int builtin_kill(DSState &state, ArrayObject &argvObj) {
  int sigNum = SIGTERM;
  bool listing = false;

  if (argvObj.getValues().size() == 1) {
    return showUsage(argvObj);
  }

  GetOptState optState;
  const int opt = optState(argvObj, ":ls:h");
  switch (opt) {
  case 'l':
    listing = true;
    break;
  case 's':
  case '?': {
    StringRef sigStr = optState.optArg;
    if (opt == '?') { // skip prefix '-', ex. -9
      sigStr = argvObj.getValues()[optState.index++].asStrRef().substr(1);
    }
    sigNum = toSigNum(sigStr);
    if (sigNum == -1) {
      ERROR(argvObj, "%s: invalid signal specification", toPrintable(sigStr).c_str());
      return 1;
    }
    break;
  }
  case 'h':
    return showHelp(argvObj);
  case ':':
    ERROR(argvObj, "-%c: option requires argument", optState.optOpt);
    return 1;
  default:
    break;
  }

  auto begin = argvObj.getValues().begin() + optState.index;
  const auto end = argvObj.getValues().end();

  if (begin == end) {
    if (listing) {
      auto sigList = getUniqueSignalList();
      unsigned int size = sigList.size();
      for (unsigned int i = 0; i < size; i++) {
        printf("%2d) SIG%s", sigList[i], getSignalName(sigList[i]));
        if (i % 5 == 4 || i == size - 1) {
          fputc('\n', stdout);
        } else {
          fputc('\t', stdout);
        }
      }
      return 0;
    }
    return showUsage(argvObj);
  }

  unsigned int count = 0;
  for (; begin != end; ++begin) {
    auto arg = begin->asStrRef();
    if (listing) {
      if (!printNumOrName(arg)) {
        count++;
        ERROR(argvObj, "%s: invalid signal specification", toPrintable(arg).c_str());
      }
    } else {
      if (killProcOrJob(state.jobTable, argvObj, arg, sigNum)) {
        count++;
      }
    }
  }

  state.jobTable.waitForAny(); // update killed process state
  if (listing && count > 0) {
    return 1;
  }
  if (!listing && count == 0) {
    return 1;
  }
  return 0;
}

static Job tryToGetJob(const JobTable &table, StringRef name, bool needPrefix) {
  if (name.startsWith("%")) {
    name.removePrefix(1);
  } else if (needPrefix) {
    return nullptr;
  }
  Job job;
  auto pair = toInt32(name);
  if (pair && pair.value > -1) {
    job = table.find(pair.value);
  }
  return job;
}

int builtin_fg_bg(DSState &state, ArrayObject &argvObj) {
  if (!state.isJobControl()) {
    ERROR(argvObj, "no job control in this shell");
    return 1;
  }

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "h")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(argvObj, optState);
    }
  }

  bool fg = argvObj.getValues()[0].asStrRef() == "fg";
  const unsigned int size = argvObj.getValues().size();
  unsigned int index = optState.index;
  Job job;
  StringRef arg = "current";
  if (index == size) {
    job = state.jobTable.syncAndGetCurPrevJobs().cur;
  } else {
    arg = argvObj.getValues()[index].asStrRef();
    job = tryToGetJob(state.jobTable, arg, false);
  }

  int ret = 0;
  if (job) {
    auto fmt = JobInfoFormat::DESC;
    if (fg) {
      beForeground(job->getValidPid(0));
    } else {
      setFlag(fmt, JobInfoFormat::JOB_ID);
    }
    job->showInfo(stdout, fmt);
    job->send(SIGCONT);
    state.jobTable.waitForAny();
  } else {
    ERROR(argvObj, "%s: no such job", toPrintable(arg).c_str());
    ret = 1;
    if (fg) {
      return ret;
    }
  }

  if (fg) {
    int s = state.jobTable.waitForJob(job, WaitOp::BLOCK_UNTRACED, true); // FIXME: check root shell
    int errNum = errno;
    if (job->isRunning()) {
      state.jobTable.setCurrentJob(job);
      job->showInfo();
    } else if (job->isTerminated()) {
      job->lastProc().showSignal();
    }
    state.tryToBeForeground();
    if (errNum != 0) {
      errno = errNum;
      PERROR(argvObj, "wait failed");
    }
    return s;
  }

  // process remain arguments
  for (unsigned int i = index + 1; i < size; i++) {
    arg = argvObj.getValues()[i].asStrRef();
    job = tryToGetJob(state.jobTable, arg, false);
    if (job) {
      job->showInfo(stdout, JobInfoFormat::JOB_ID | JobInfoFormat::DESC);
      job->send(SIGCONT);
    } else {
      ERROR(argvObj, "%s: no such job", toPrintable(arg).c_str());
      ret = 1;
    }
  }
  state.jobTable.waitForAny();
  return ret;
}

int builtin_wait(DSState &state, ArrayObject &argvObj) {
  bool breakNext = false;
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "nh")) != -1;) {
    switch (opt) {
    case 'n':
      breakNext = true;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  const WaitOp op = state.isJobControl() ? WaitOp::BLOCK_UNTRACED : WaitOp::BLOCKING;
  auto cleanup = finally([&] { state.jobTable.waitForAny(); });

  std::vector<std::pair<Job, int>> targets;
  if (optState.index == argvObj.size()) {
    for (auto &j : state.jobTable) {
      if (j->isDisowned()) {
        continue;
      }
      targets.emplace_back(j, -1);
    }
  }
  for (unsigned int i = optState.index; i < argvObj.size(); i++) {
    auto ref = argvObj.getValues()[i].asStrRef();
    auto target = parseProcOrJob(state.jobTable, argvObj, ref, false);
    Job job;
    int offset = -1;
    if (is<Job>(target)) {
      job = std::move(get<Job>(target));
    } else if (is<const ProcTable::Entry *>(target)) {
      auto *e = get<const ProcTable::Entry *>(target);
      job = state.jobTable.find(e->jobId());
      assert(job);
      offset = e->procOffset();
    } else {
      return 127;
    }
    targets.emplace_back(std::move(job), offset);
  }

  // wait jobs
  int lastStatus = 0;
  if (breakNext) {
    do {
      for (auto &target : targets) {
        if (!target.first->isRunning()) {
          return target.first->exitStatus();
        }
      }
    } while ((lastStatus = state.jobTable.waitForJob(nullptr, op)) > -1);
  } else {
    for (auto &target : targets) {
      lastStatus = state.jobTable.waitForJob(target.first, op);
      if (lastStatus < 0) {
        break;
      }
      if (target.second != -1 &&
          !target.first->getProcs()[target.second].is(Proc::State::RUNNING)) {
        lastStatus = target.first->getProcs()[target.second].exitStatus();
      }
    }
  }
  return lastStatus;
}

enum class JobsTarget {
  ALL,
  RUNNING,
  STOPPED,
};

enum class JobsOutput {
  DEFAULT,
  PID_ONLY,
  VERBOSE,
};

static void showJobInfo(const JobTable::CurPrevJobs &entry, JobsTarget target, JobsOutput output,
                        const Job &job) {
  if (job->isDisowned()) {
    return; // always ignore disowned jobs
  }
  switch (target) {
  case JobsTarget::ALL:
    break;
  case JobsTarget::RUNNING:
    if (!job->getProcs()[0].is(Proc::State::RUNNING)) {
      return;
    }
    break;
  case JobsTarget::STOPPED:
    if (!job->getProcs()[0].is(Proc::State::STOPPED)) {
      return;
    }
    break;
  }

  JobInfoFormat fmt{};
  switch (output) {
  case JobsOutput::PID_ONLY:
    fmt = JobInfoFormat::PID;
    break;
  case JobsOutput::DEFAULT:
  case JobsOutput::VERBOSE:
    fmt = JobInfoFormat::JOB_ID | JobInfoFormat::STATE | JobInfoFormat::DESC;
    setFlag(fmt, entry.getJobType(job));
    if (output == JobsOutput::VERBOSE) {
      setFlag(fmt, JobInfoFormat::PID | JobInfoFormat::VERBOSE);
    }
    break;
  }
  job->showInfo(stdout, fmt);
}

int builtin_jobs(DSState &state, ArrayObject &argvObj) {
  auto target = JobsTarget::ALL;
  auto output = JobsOutput::DEFAULT;

  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "lprsh")) != -1;) {
    switch (opt) {
    case 'l':
      output = JobsOutput::VERBOSE;
      break;
    case 'p':
      output = JobsOutput::PID_ONLY;
      break;
    case 'r':
      target = JobsTarget::RUNNING;
      break;
    case 's':
      target = JobsTarget::STOPPED;
      break;
    case 'h':
      return showHelp(argvObj);
    default:
      return invalidOptionError(argvObj, optState);
    }
  }

  auto &entry = state.jobTable.syncAndGetCurPrevJobs();
  if (optState.index == argvObj.size()) { // show all jobs
    for (auto &job : state.jobTable) {
      showJobInfo(entry, target, output, job);
    }
    return 0;
  }

  // show specified jobs
  bool hasError = false;
  for (unsigned int i = optState.index; i < argvObj.size(); i++) {
    auto ref = argvObj.getValues()[i].asStrRef();
    auto job = tryToGetJob(state.jobTable, ref, true);
    if (!job) {
      ERROR(argvObj, "%s: no such job", toPrintable(ref).c_str());
      hasError = true;
      continue;
    }
    showJobInfo(entry, target, output, job);
  }
  return hasError ? 1 : 0;
}

int builtin_disown(DSState &state, ArrayObject &argvObj) {
  GetOptState optState;
  for (int opt; (opt = optState(argvObj, "lprsh")) != -1;) {
    if (opt == 'h') {
      return showHelp(argvObj);
    } else {
      return invalidOptionError(argvObj, optState);
    }
  }

  if (optState.index == argvObj.size()) {
    auto job = state.jobTable.syncAndGetCurPrevJobs().cur;
    if (!job) {
      ERROR(argvObj, "current: no such job");
      return 1;
    }
    job->disown();
    return 0;
  }
  for (unsigned int i = optState.index; i < argvObj.size(); i++) {
    auto ref = argvObj.getValues()[i].asStrRef();
    auto job = tryToGetJob(state.jobTable, ref, true);
    if (!job) {
      ERROR(argvObj, "%s: no such job", toPrintable(ref).c_str());
      return 1;
    }
    job->disown();
  }
  return 0;
}

} // namespace ydsh