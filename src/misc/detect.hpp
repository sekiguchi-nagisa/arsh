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


#ifndef YDSH_MISC_DETECT_HPP
#define YDSH_MISC_DETECT_HPP

#include <type_traits>

namespace ydsh {

template <bool B>
using enable_when = std::enable_if_t<B, std::nullptr_t>;

namespace __detail {

template <typename ...T>
struct VoidHolder {
    using type = void;
};

} // namespace __detail

template <typename ...T>
using void_t = typename __detail::VoidHolder<T...>::type;

namespace __detail {

template <typename, template<typename ...> class, typename ...>
struct detector : std::false_type {};

template <template<typename ...> class OP, typename ...Arg>
struct detector<void_t<OP<Arg...>>, OP, Arg...> : std::true_type {};

} // namespace __detail

template <template<typename ...> class OP, typename ...Arg>
constexpr auto is_detected_v = __detail::detector<void_t<OP<Arg...>>, OP, Arg...>::value;

} // namespace ydsh

#endif //YDSH_MISC_DETECT_HPP
