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

#include "code_buffer.h"

#include <sys/mman.h>
#include <unistd.h>

#include "misc/resource.hpp"

namespace arsh::regex::jit {

CodeBuffer::~CodeBuffer() {
  if (this->ptr) {
    munmap(this->ptr, this->capacity);
  }
}

bool CodeBuffer::allocate(const size_t size) {
  const long pageSize = sysconf(_SC_PAGESIZE);
  if (pageSize <= 0) {
    return false;
  }
  this->capacity = (size + pageSize - 1) / pageSize * pageSize;
  void *ptr = mmap(nullptr, this->capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                   0);
  if (ptr == MAP_FAILED) {
    this->ptr = nullptr;
    this->capacity = 0;
    return false;
  }
  this->ptr = static_cast<uint8_t *>(ptr);
  return true;
}

bool CodeBuffer::makeExecutable() {
  if (!this->ptr || this->executable) {
    return false;
  }
  if (mprotect(this->ptr, this->capacity, PROT_READ | PROT_EXEC) != 0) {
    return false;
  }
  this->executable = true;
  return true;
}

} // namespace arsh::regex::jit
