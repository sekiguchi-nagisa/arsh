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

#ifndef ARSH_COMPARE_H
#define ARSH_COMPARE_H

namespace arsh {

class Value;

class Equality {
private:
  const bool partial;
  bool overflow{false};

  static constexpr unsigned int STACK_DEPTH_LIMIT = 256;

public:
  explicit Equality(bool partial = false) : partial(partial) {}

  bool hasOverflow() const { return this->overflow; }

  bool operator()(const Value &x, const Value &y);
};

class Ordering {
private:
  bool overflow{false};

  static constexpr unsigned int STACK_DEPTH_LIMIT = 256;

public:
  bool hasOverflow() const { return this->overflow; }

  int operator()(const Value &x, const Value &y);
};

} // namespace arsh

#endif // ARSH_COMPARE_H
