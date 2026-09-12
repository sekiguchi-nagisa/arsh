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

#ifndef ARSH_REGEX_JIT_JIT_CODE_H
#define ARSH_REGEX_JIT_JIT_CODE_H

#include <cstdint>
#include <memory>
#include <vector>

#include "code_buffer.h"

namespace arsh::regex {

class Regex;

namespace jit {

/**
 * native code compiled from one `Regex`.
 *
 * one block is emitted per bytecode instruction, in bytecode order, so `codeOffsets[offset]` gives
 * the offset within the buffer of the block for the instruction that starts at that bytecode byte
 * offset. the driver resolves its re-entry point through this table.
 */
struct JitCode {
  CodeBuffer buffer;
  std::vector<uint32_t> codeOffsets; // bytecode byte offset -> block offset within `buffer`

  JitCode() = default;

  NON_COPYABLE(JitCode);

  const uint8_t *code() const { return this->buffer.data(); }
};

/**
 * compile the bytecode of `regex` into native code.
 *
 * returns nullptr when the pattern cannot be compiled (an unknown instruction, an unsupported
 * relocatable form, an allocation failure). the caller then falls back to the interpreter.
 */
std::shared_ptr<JitCode> compile(const Regex &regex);

} // namespace jit

} // namespace arsh::regex

#endif // ARSH_REGEX_JIT_JIT_CODE_H
