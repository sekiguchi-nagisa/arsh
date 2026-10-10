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

#ifndef MISC_LIB_FUNCTION_REF_HPP
#define MISC_LIB_FUNCTION_REF_HPP

#include <memory>

#include "detect.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

/*
 * similar to llvm::function_ref / std::function_ref (C++26)
 * only accept function ptr or function object (not accept member function pointer)
 */
template <typename T>
class FunctionRef;

template <typename R, typename... Arg>
class FunctionRef<R(Arg...)> {
private:
  using Invoker = R (*)(uintptr_t, Arg...);
  using FunctionPtr = R (*)(Arg...);

  uintptr_t callable_{0}; // pointer to function or object (type-erased)
  Invoker invoker_{nullptr};

  template <typename F>
  static R invoke(const uintptr_t callable, Arg... arg) {
    using Callable = std::remove_reference_t<F>;
    if constexpr (std::is_convertible_v<Callable &, FunctionPtr>) {
      auto func = reinterpret_cast<FunctionPtr>(callable);
      if constexpr (std::is_same_v<void, R>) {
        (*func)(std::forward<Arg>(arg)...);
        return;
      } else {
        return (*func)(std::forward<Arg>(arg)...);
      }
    } else {
      auto &func = *reinterpret_cast<Callable *>(callable);
      if constexpr (std::is_same_v<void, R>) {
        func(std::forward<Arg>(arg)...);
        return;
      } else {
        return func(std::forward<Arg>(arg)...);
      }
    }
  }

  template <typename F>
  static auto toTarget(F &&func) noexcept {
    using Callable = std::remove_reference_t<F>;
    if constexpr (std::is_convertible_v<Callable &, FunctionPtr>) {
      return static_cast<FunctionPtr>(func); // function / function ptr / capture-less lambda
    } else {
      return std::addressof(func); // lvalue function object
    }
  }

public:
  using result_type = R;

  constexpr FunctionRef() noexcept = default;

  constexpr FunctionRef(std::nullptr_t) noexcept {} // NOLINT

  constexpr FunctionRef(R (*func)(Arg...)) noexcept // NOLINT
      : callable_(reinterpret_cast<uintptr_t>(func)),
        invoker_([](const uintptr_t callable, Arg... arg) -> R {
          return invoke<FunctionPtr>(callable, std::forward<Arg>(arg)...);
        }) {}

  template <typename F, typename T = std::remove_cv_t<std::remove_reference_t<F>>,
            enable_when<!std::is_same_v<FunctionRef, T> && std::is_invocable_r_v<R, T &, Arg...> &&
                        (std::is_convertible_v<T &, FunctionPtr> ||
                         (std::is_lvalue_reference_v<F> && std::is_object_v<T>))> = nullptr>
  constexpr FunctionRef(F &&func) noexcept // NOLINT
      : callable_(reinterpret_cast<uintptr_t>(toTarget<F>(std::forward<F>(func)))),
        invoker_([](const uintptr_t callable, Arg... arg) -> R {
          using Callable = std::remove_reference_t<F>;
          if constexpr (std::is_convertible_v<Callable &, FunctionPtr>) {
            return invoke<FunctionPtr>(callable, std::forward<Arg>(arg)...);
          } else {
            return invoke<F>(callable, std::forward<Arg>(arg)...);
          }
        }) {}

  explicit operator bool() const noexcept { return this->invoker_ != nullptr; }

  R operator()(Arg... arg) const {
    return this->invoker_(this->callable_, std::forward<Arg>(arg)...);
  }
};

template <typename R, typename... Arg>
FunctionRef(R (*)(Arg...)) -> FunctionRef<R(Arg...)>;

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_FUNCTION_REF_HPP
