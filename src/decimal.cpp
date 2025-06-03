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

#include <cmath>

#include "../external/dragonbox/simple_dragonbox.h"

#include "decimal.h"
#include "misc/num_util.hpp"

namespace arsh {

// #####################
// ##     Decimal     ##
// #####################

bool Decimal::create(double value, Decimal &out) {
  if (!std::isfinite(value)) {
    return false;
  }
  if (value == 0.0) {
    out = {0, 0, std::signbit(value)};
  } else {
    auto [significand, exponent, sign] = jkj::simple_dragonbox::to_decimal(
        value, jkj::simple_dragonbox::policy::cache::compact,
        jkj::simple_dragonbox::policy::binary_to_decimal_rounding::to_even);
    out = {.significand = significand, .exponent = exponent, .sign = sign};
  }
  return true;
}

std::string Decimal::toString() const {
  std::string ret;
  ret += std::to_string(this->significand);
  if (this->exponent >= 0) {
    if (this->exponent <= 6) {
      ret.append(this->exponent, '0');
      ret += ".0";
    } else {
      ret += "e+";
      ret += std::to_string(this->exponent);
    }
  } else {
    const unsigned int count = ret.size();
    if (const unsigned int exp = std::abs(this->exponent); count > exp) {
      ret.insert(ret.end() - exp, '.');
    } else if (count <= exp && exp - count <= 3) {
      ret.insert(0, exp - count, '0');
      ret.insert(0, "0.");
    } else {
      ret += 'e';
      ret += std::to_string(this->exponent);
    }
  }
  if (this->sign) {
    ret.insert(0, "-");
  }
  return ret;
}

} // namespace arsh