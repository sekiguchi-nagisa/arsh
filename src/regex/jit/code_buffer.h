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

#ifndef ARSH_REGEX_JIT_CODE_BUFFER_H
#define ARSH_REGEX_JIT_CODE_BUFFER_H

#include <cstddef>
#include <cstdint>

#include "misc/resource.hpp"

namespace arsh::regex::jit {

/**
 * an executable memory buffer.
 *
 * the buffer is created writable so the copy-and-patch compiler can copy the stencil code and patch
 * the resolved addresses, then made executable (`PROT_READ|PROT_EXEC`) once. the write permission is
 * dropped afterwards to comply with W^X.
 */
class CodeBuffer {
private:
  uint8_t *ptr{nullptr};
  size_t capacity{0};
  bool executable{false};

public:
  CodeBuffer() = default;

  NON_COPYABLE(CodeBuffer);

  ~CodeBuffer();

  /**
   * allocate `size` bytes of read-write memory.
   * @return false on allocation failure
   */
  bool allocate(size_t size);

  uint8_t *data() { return this->ptr; }

  const uint8_t *data() const { return this->ptr; }

  size_t size() const { return this->capacity; }

  /**
   * drop the write permission and allow execution.
   * @return false on failure
   */
  bool makeExecutable();
};

} // namespace arsh::regex::jit

#endif // ARSH_REGEX_JIT_CODE_BUFFER_H
