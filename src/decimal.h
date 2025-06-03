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

#ifndef ARSH_DECIMAL_H
#define ARSH_DECIMAL_H

#include <cstdint>
#include <string>

namespace arsh {

struct Decimal {
  uint64_t significand;
  int exponent;
  bool sign;

  /**
   * convert double to decimal
   * @param value
   * @param out
   * @return
   * if failed (nan or inf), return false
   */
  static bool create(double value, Decimal &out);

  std::string toString() const;
};

} // namespace arsh

#endif // ARSH_DECIMAL_H
