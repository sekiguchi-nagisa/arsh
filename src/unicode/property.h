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

#ifndef ARSH_UNICODE_PROPERTY_H
#define ARSH_UNICODE_PROPERTY_H

#include "../misc/result.hpp"
#include "../misc/string_ref.hpp"
#include "codepoint_set_ref.hpp"

namespace arsh {
namespace ucp {

// for Unicode general category

#define EACH_UCP_CATEGORY(E)                                                                       \
  E(Lu)                                                                                            \
  E(Ll)                                                                                            \
  E(Lt)                                                                                            \
  E(Lm)                                                                                            \
  E(Lo)                                                                                            \
  E(Mn)                                                                                            \
  E(Mc)                                                                                            \
  E(Me)                                                                                            \
  E(Nd)                                                                                            \
  E(Nl)                                                                                            \
  E(No)                                                                                            \
  E(Pc)                                                                                            \
  E(Pd)                                                                                            \
  E(Ps)                                                                                            \
  E(Pe)                                                                                            \
  E(Pi)                                                                                            \
  E(Pf)                                                                                            \
  E(Po)                                                                                            \
  E(Sm)                                                                                            \
  E(Sc)                                                                                            \
  E(Sk)                                                                                            \
  E(So)                                                                                            \
  E(Zs)                                                                                            \
  E(Zl)                                                                                            \
  E(Zp)                                                                                            \
  E(Cc)                                                                                            \
  E(Cf)                                                                                            \
  E(Cs)                                                                                            \
  E(Co)                                                                                            \
  E(Cn)

enum class Category : unsigned char {
#define GEN_ENUM(E) E,
  EACH_UCP_CATEGORY(GEN_ENUM)
#undef GEN_ENUM
};

Optional<Category> parseCategory(StringRef ref);

/**
 * * @param category
 * @return
 * if pass invalid category, return empty
 */
CodePointSetRef getCategorySet(Category category);

Optional<Category> getCategory(int codePoint);

} // namespace ucp

const char *toString(ucp::Category category);

} // namespace arsh

#endif // ARSH_UNICODE_PROPERTY_H
