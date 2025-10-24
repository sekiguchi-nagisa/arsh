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

#include "constant.h"

struct ARState;

namespace arsh {

class Value;
class ArrayObject;
class CandidatesObject;
class TypePool;

class Equality {
private:
  const bool partial;
  bool overflow{false};

  static constexpr unsigned int STACK_DEPTH_LIMIT = SYS_LIMIT_NESTED_OBJ_DEPTH;

public:
  explicit Equality(bool partial = false) : partial(partial) {}

  bool hasOverflow() const { return this->overflow; }

  bool operator()(const Value &x, const Value &y);
};

class Ordering {
private:
  bool overflow{false};

  static constexpr unsigned int STACK_DEPTH_LIMIT = SYS_LIMIT_NESTED_OBJ_DEPTH;

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

  static constexpr unsigned int STACK_DEPTH_LIMIT = SYS_LIMIT_NESTED_OBJ_DEPTH;

public:
  Stringifier(const TypePool &pool, Appender &appender) : pool(pool), appender(appender) {}

  bool hasOverflow() const { return this->overflow; }

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

  bool addCandidateAsStr(const CandidatesObject &obj, unsigned int index);

private:
  /**
   * for non-recursive type
   * @param value
   * @return
   */
  bool addAsFlatStr(const Value &value);
};

class StrAppender : public Stringifier::Appender {
public:
  enum class Op : unsigned char {
    APPEND,    // just append
    PRINTABLE, // append as a printable string
  };

private:
  std::string buf;
  const unsigned int limit;
  Op appendOp{Op::APPEND};

public:
  explicit StrAppender(unsigned int limit) : limit(limit) {}

  bool operator()(StringRef ref) override;

  std::string take() && { return std::move(this->buf); }

  const std::string &get() const { return this->buf; }

  Op getAppendOp() const { return this->appendOp; }

  void setAppendOp(Op op) { this->appendOp = op; }
};

class StrObjAppender : public Stringifier::Appender {
private:
  ARState &state;
  Value &value; // must be String

public:
  StrObjAppender(ARState &state, Value &value) : state(state), value(value) {}

  bool operator()(StringRef ref) override;
};

/**
 * build command argument list
 * @param state
 * @param value
 * @param argv
 * @param redir
 * maybe null, invalid or RedirObject
 * @return
 * if error, return false
 */
bool addAsCmdArg(ARState &state, Value &&value, ArrayObject &argv, Value &redir);

} // namespace arsh

#endif // ARSH_COMPARE_H
