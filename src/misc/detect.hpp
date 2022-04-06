/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#ifndef MISC_LIB_DETECT_HPP
#define MISC_LIB_DETECT_HPP

#include <type_traits>

#include "noncopyable.h"

BEGIN_MISC_LIB_NAMESPACE_DECL

template <bool B>
using enable_when = std::enable_if_t<B, std::nullptr_t>;

namespace detail {

struct NotDetected {
  NotDetected() = delete;
  ~NotDetected() = delete;
  NON_COPYABLE(NotDetected);
};

template <typename, template <typename...> class, typename...>
struct detector : std::false_type {
  using type = NotDetected;
};

template <template <typename...> class OP, typename... Arg>
struct detector<std::void_t<OP<Arg...>>, OP, Arg...> : std::true_type {
  using type = OP<Arg...>;
};

} // namespace detail

template <template <typename...> class OP, typename... Arg>
constexpr auto is_detected_v = detail::detector<void, OP, Arg...>::value;

template <template <typename...> class OP, typename... Arg>
using detected_t = typename detail::detector<void, OP, Arg...>::type;

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_DETECT_HPP
