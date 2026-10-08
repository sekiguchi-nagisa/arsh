/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

/**
 * Adapter which registers the regexp engine used by Node.js (V8) into the
 * `rust-leipzig/regex-performance` benchmark tool.
 *
 * Node.js is not embeddable here (the system installation only ships the `node` executable, not a
 * linkable `libnode`), so this adapter runs the engine out of process: it starts the installed
 * `node` executable on the small JavaScript program below, feeds it the subject on standard input,
 * has it compile the pattern once and scan the subject `repeat` times, and reads back the per-run
 * times and the match count. Only the scan itself is timed (the JavaScript side measures with
 * `performance.now()`); process start-up and reading the subject are not part of the measured
 * region.
 *
 * Because the engine lives in a separate process, the in-process heap tracker cannot observe its
 * allocations, so the memory columns stay `0` (see the README).
 *
 * See `nodejs_find_all` for the benchmark entry point required by the harness (see main.h).
 */

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "main.h"

#ifndef NODEJS_EXECUTABLE
#define NODEJS_EXECUTABLE "node"
#endif

/**
 * JavaScript program run by the `node` executable. Arguments (after `-e`):
 *   argv[1] = pattern, argv[2] = repeat count. The subject is read from standard input.
 *
 * A leading inline `(?ims)` modifier (used by some benchmark patterns) is translated to the
 * equivalent JavaScript flag, because JavaScript has no inline flag syntax. The pattern is
 * always compiled with the `u` (Unicode) flag, matching what the other Unicode engines do.
 *
 * Output (single line):
 *   `RESULT <matches> <t0> <t1> ...` on success (times in milliseconds),
 *   `ERROR <message>` if the pattern cannot be compiled or the scan throws.
 */
static const char *const NODEJS_SCRIPT = R"NODEJS(
"use strict";
const fs = require("fs");
const patternArg = process.argv[1];
const repeat = Math.max(1, parseInt(process.argv[2], 10) || 1);
let pattern = patternArg;
let flags = "gu";
if (pattern.length >= 4 && pattern[0] === "(" && pattern[1] === "?" && pattern[3] === ")" &&
    "ims".indexOf(pattern[2]) >= 0) {
  flags += pattern[2];
  pattern = pattern.slice(4);
}
let re;
try {
  re = new RegExp(pattern, flags);
} catch (e) {
  process.stdout.write("ERROR " + e.message + "\n");
  process.exit(0);
}
const subject = fs.readFileSync(0, "utf8");
let matches = 0;
const times = [];
try {
  for (let i = 0; i < repeat; i++) {
    const start = performance.now();
    const found = subject.match(re);
    const end = performance.now();
    matches = found ? found.length : 0;
    times.push(end - start);
  }
} catch (e) {
  process.stdout.write("ERROR " + e.message + "\n");
  process.exit(0);
}
process.stdout.write("RESULT " + matches + " " + times.map((t) => t.toFixed(6)).join(" ") + "\n");
)NODEJS";

/** Write the whole buffer, retrying short writes. Returns false on error. */
static bool writeAll(int fd, const char *data, size_t size) {
  while (size > 0) {
    const ssize_t written = write(fd, data, size);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (written == 0) {
      return false;
    }
    data += written;
    size -= static_cast<size_t>(written);
  }
  return true;
}

/**
 * Start the Node.js executable with `subject` on its standard input and return the captured
 * standard output. Returns an empty string if the process cannot be started or exits with a
 * non-zero status.
 */
static std::string runNode(const std::string &pattern, const char *subject, int subjectLen,
                           int repeat) {
  const std::string repeatStr = std::to_string(repeat);
  const char *argv[] = {NODEJS_EXECUTABLE, "-e", NODEJS_SCRIPT, pattern.c_str(), repeatStr.c_str(),
                        nullptr};

  int inPipe[2];
  int outPipe[2];
  if (pipe(inPipe) != 0) {
    return {};
  }
  if (pipe(outPipe) != 0) {
    close(inPipe[0]);
    close(inPipe[1]);
    return {};
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(inPipe[0]);
    close(inPipe[1]);
    close(outPipe[0]);
    close(outPipe[1]);
    return {};
  }
  if (pid == 0) {
    close(inPipe[1]);
    close(outPipe[0]);
    if (dup2(inPipe[0], STDIN_FILENO) < 0 || dup2(outPipe[1], STDOUT_FILENO) < 0) {
      _exit(127);
    }
    close(inPipe[0]);
    close(outPipe[1]);
    execvp(NODEJS_EXECUTABLE, const_cast<char *const *>(argv));
    _exit(127);
  }

  close(inPipe[0]);
  close(outPipe[1]);
  /* the child reads all of the subject before producing output, so this cannot deadlock. If
     `node` cannot be started the pipe closes early; ignore SIGPIPE while writing so the write
     fails with EPIPE instead of killing the whole benchmark. */
  void (*oldSigpipe)(int) = signal(SIGPIPE, SIG_IGN);
  const bool sent = writeAll(inPipe[1], subject, static_cast<size_t>(subjectLen));
  signal(SIGPIPE, oldSigpipe);
  if (!sent) {
    close(inPipe[1]);
    close(outPipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    fprintf(stderr, "nodejs: failed to send the subject to '%s'\n", NODEJS_EXECUTABLE);
    return {};
  }
  close(inPipe[1]);

  std::string out;
  char buf[4096];
  for (;;) {
    const ssize_t n = read(outPipe[0], buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (n == 0) {
      break;
    }
    out.append(buf, static_cast<size_t>(n));
  }
  close(outPipe[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "nodejs: '%s' exited abnormally (status %d)\n", NODEJS_EXECUTABLE, status);
    return {};
  }
  return out;
}

extern "C" int nodejs_find_all(char *pattern, char *subject, int subject_len, int repeat,
                               struct result *res) {
  if (pattern == nullptr || subject == nullptr || subject_len < 0 || repeat <= 0 || res == nullptr) {
    return -1;
  }
  const std::string out = runNode(pattern, subject, subject_len, repeat);
  if (out.empty()) {
    return -1;
  }

  const char *const resultTag = "RESULT ";
  const size_t pos = out.find(resultTag);
  if (pos == std::string::npos) {
    if (out.compare(0, 6, "ERROR ") == 0) {
      printf("nodejs compilation failed: %s (%s)\n", pattern, out.c_str() + 6);
    } else {
      fprintf(stderr, "nodejs: unexpected output\n");
    }
    return -1;
  }

  const char *p = out.c_str() + pos + strlen(resultTag);
  char *endPtr = nullptr;
  const long long matches = strtoll(p, &endPtr, 10);
  if (endPtr == p) {
    return -1;
  }
  p = endPtr;

  double *times = static_cast<double *>(calloc(static_cast<size_t>(repeat), sizeof(double)));
  if (times == nullptr) {
    return -1;
  }
  for (int i = 0; i < repeat; i++) {
    while (*p == ' ') {
      p++;
    }
    const double t = strtod(p, &endPtr);
    if (endPtr == p) {
      free(times);
      return -1;
    }
    times[i] = t;
    p = endPtr;
  }

  res->matches = static_cast<int>(matches);
  get_mean_and_derivation(times, static_cast<uint32_t>(repeat), res);
  free(times);
  return 0;
}
