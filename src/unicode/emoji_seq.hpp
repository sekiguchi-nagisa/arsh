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

#ifndef ARSH_UNICODE_EMOJI_SEQ_HPP
#define ARSH_UNICODE_EMOJI_SEQ_HPP

#include "misc/enum_util.hpp"

namespace arsh::ucp {

#define EACH_RGI_EMOJI_SEQ(E)                                                                      \
  E(Basic_Emoji, (1u << 0u))                                                                       \
  E(Emoji_Keycap_Sequence, (1u << 1u))                                                             \
  E(RGI_Emoji_Modifier_Sequence, (1u << 2u))                                                       \
  E(RGI_Emoji_Flag_Sequence, (1u << 3u))                                                           \
  E(RGI_Emoji_Tag_Sequence, (1u << 4u))                                                            \
  E(RGI_Emoji_ZWJ_Sequence, (1u << 5u))                                                            \
  E(RGI_Emoji, ((1u << 6u) - 1u))

enum class RGIEmojiSeq : unsigned char {
  None = 0,
#define GEN_ENUM(E, B) E = (B),
  EACH_RGI_EMOJI_SEQ(GEN_ENUM)
#undef GEN_ENUM
      CASE_IGNORE = 1u << 7u,
};

} // namespace arsh::ucp

namespace arsh {
template <>
struct allow_enum_bitop<ucp::RGIEmojiSeq> : std::true_type {};
} // namespace arsh

#endif // ARSH_UNICODE_EMOJI_SEQ_HPP
