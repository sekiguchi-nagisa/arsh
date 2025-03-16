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

#include <cstdlib>

#define XXH_STATIC_LINKING_ONLY /* access advanced declarations */
#define XXH_IMPLEMENTATION      /* access definitions */

#include "../external/xxHash/xxhash.h"
#include "hasher.h"

namespace arsh::lsp {

XXHasher::XXHasher(const uint64_t seed) : state(nullptr) {
  auto *st = XXH3_createState();
  if (!st || XXH3_64bits_reset_withSeed(st, seed) == XXH_ERROR) {
    abort();
  }
  this->state = st;
}

XXHasher::~XXHasher() {
  if (this->state) {
    XXH3_freeState(static_cast<XXH3_state_t *>(this->state));
  }
}

bool XXHasher::update(const void *data, size_t len) {
  return XXH3_64bits_update(static_cast<XXH3_state_t *>(this->state), data, len) == XXH_OK;
}

uint64_t XXHasher::digest() && {
  static_assert(sizeof(uint64_t) == sizeof(XXH64_hash_t));
  return XXH3_64bits_digest(static_cast<XXH3_state_t *>(this->state));
}

} // namespace arsh::lsp