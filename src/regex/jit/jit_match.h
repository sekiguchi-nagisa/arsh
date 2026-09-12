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

#ifndef ARSH_REGEX_JIT_JIT_MATCH_H
#define ARSH_REGEX_JIT_JIT_MATCH_H

#include "../match_context.h"
#include "../matcher.h"

namespace arsh::regex::jit {

struct JitCode;

/**
 * run `code` against `ctx`.
 *
 * this is the JIT counterpart of `match()` in `vm.cpp`: it starts the first search, then runs the
 * JIT blocks, falling back to the backtrack stack when a block reports a failure. it mirrors the
 * interpreter's control flow exactly so that both produce the same captures.
 */
MatchStatus jitMatch(MatchContext &ctx, const JitCode &code, ObserverPtr<Timer> timer);

} // namespace arsh::regex::jit

#endif // ARSH_REGEX_JIT_JIT_MATCH_H
