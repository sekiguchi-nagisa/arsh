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

#include "misc/string_ref.hpp"

struct ARState;

namespace arsh {

class Value;
class TypePool;

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

class Stringifier {
public:
  struct Appender {
    virtual ~Appender() = default;

    /**
     * @param ref
     * @return
     * if reach limit, return false
     */
    virtual bool operator()(StringRef ref) = 0;
  };

private:
  const TypePool &pool;
  Appender &appender;
  bool overflow{false};
  bool truncated{false};

  static constexpr unsigned int STACK_DEPTH_LIMIT = 256;

public:
  Stringifier(const TypePool &pool, Appender &appender) : pool(pool), appender(appender) {}

  bool hasOverflow() const { return this->overflow; }

  bool isTruncated() const { return this->truncated; }

  /**
   * for to-string (OP_STR)
   * @param value
   * @return
   */
  bool addAsStr(const Value &value);

  /**
   * for string interpolation (OP_INTERP)
   * @param value
   * @return
   */
  bool addAsInterp(const Value &value);

private:
  /**
   * for non-recursive type
   * @param value
   * @return
   */
  bool addAsFlatStr(const Value &value);
};

class StrAppender : public Stringifier::Appender {
private:
  std::string buf;
  const unsigned int limit;

public:
  explicit StrAppender(unsigned int limit) : limit(limit) {}

  bool operator()(StringRef ref) override;

  std::string take() && { return std::move(this->buf); }
};

class StrObjAppender : public Stringifier::Appender {
private:
  ARState &state;
  Value &value;

public:
  StrObjAppender(ARState &state, Value &value) : state(state), value(value) {}

  bool operator()(StringRef ref) override;
};

} // namespace arsh

#endif // ARSH_COMPARE_H
