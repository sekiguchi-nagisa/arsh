/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_ANALYZER_HASHER_H
#define ARSH_TOOLS_ANALYZER_HASHER_H

namespace arsh::lsp {

class XXHasher {
private:
  void *state; // XXH3_state_t

public:
  explicit XXHasher(uint64_t seed);

  ~XXHasher();

  bool update(const void *data, size_t len);

  uint64_t digest() &&;
};

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_HASHER_H
