/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#ifndef MISC_LIB_LOCALE_HPP
#define MISC_LIB_LOCALE_HPP

#include <clocale>
#include <new>
#include <utility>

#ifdef __APPLE__
#include <xlocale.h>
#endif

BEGIN_MISC_LIB_NAMESPACE_DECL

class Locale {
private:
  locale_t value{nullptr};

public:
  Locale() = default;

  Locale(int mask, const char *locale, locale_t base = nullptr)
      : value(newlocale(mask, locale, base)) {}

  Locale(Locale &&o) noexcept : value(o.value) { o.value = nullptr; }

  ~Locale() {
    if (this->value) {
      freelocale(this->value);
    }
  }

  Locale &operator=(Locale &&o) noexcept {
    if (this != std::addressof(o)) {
      this->~Locale();
      new (this) Locale(std::move(o));
    }
    return *this;
  }

  explicit operator bool() const { return this->value != nullptr; }

  locale_t get() const { return this->value; }

  void use() const { uselocale(this->value); }

  /**
   * set thread locale to global
   */
  static void restore() { uselocale(LC_GLOBAL_LOCALE); }
};

inline auto POSIX_LOCALE_C = Locale(LC_ALL_MASK, "C");

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_LOCALE_HPP
