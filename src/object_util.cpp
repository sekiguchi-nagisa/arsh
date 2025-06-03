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

#include "object_util.h"
#include "candidates.h"
#include "constant.h"
#include "core.h"
#include "decimal.h"
#include "format_util.h"
#include "job.h"
#include "misc/inlined_stack.hpp"
#include "misc/num_util.hpp"
#include "object.h"
#include "ordered_map.h"
#include "redir.h"
#include "type_pool.h"

namespace arsh {

// ######################
// ##     Equality     ##
// ######################

struct OrdFrame {
  const Value *x;
  const Value *y;
  unsigned int xi;
  unsigned int yi;

  static_assert(OrderedMapObject::MAX_SIZE * 2 < UINT32_MAX);

  OrdFrame() = default;

  OrdFrame(const Value &x, const Value &y) : x(&x), y(&y), xi(0), yi(0) {} // NOLINT
};

#define GOTO_NEXT(FS, F)                                                                           \
  do {                                                                                             \
    (FS).push(F);                                                                                  \
    if ((FS).size() == STACK_DEPTH_LIMIT) {                                                        \
      this->overflow = true;                                                                       \
      return false;                                                                                \
    }                                                                                              \
    goto NEXT;                                                                                     \
  } while (false)

static unsigned int skipEmptyEntries(const OrderedMapObject &obj, unsigned int &index) {
  for (auto &entries = obj.getEntries(); index < entries.getUsedSize() && !entries[index]; index++)
    ;
  return index;
}

static unsigned int skipEmptyEntries2(const OrderedMapObject &obj, unsigned int &index) {
  unsigned int i = index / 2;
  for (auto &entries = obj.getEntries(); i < entries.getUsedSize() && !entries[i];) {
    i++;
    index += 2;
  }
  return i;
}

bool Equality::operator()(const Value &x, const Value &y) {
  this->overflow = false;
  InlinedStack<OrdFrame, 4> frames;
  for (frames.push(OrdFrame(x, y)); frames.size(); frames.pop()) {
  NEXT: {
    auto &xp = *frames.back().x;
    auto &yp = *frames.back().y;

    // for string
    if (xp.hasStrRef() && yp.hasStrRef()) {
      if (xp.asStrRef() == yp.asStrRef()) {
        continue;
      }
      return false;
    }

    if (xp.kind() != yp.kind()) {
      return false;
    }

    switch (xp.kind()) {
    case ValueKind::EMPTY:
    case ValueKind::INVALID:
      continue;
    case ValueKind::BOOL:
      if (xp.asBool() == yp.asBool()) {
        continue;
      }
      return false;
    case ValueKind::SIG:
      if (xp.asSig() == yp.asSig()) {
        continue;
      }
      return false;
    case ValueKind::INT:
      if (xp.asInt() == yp.asInt()) {
        continue;
      }
      return false;
    case ValueKind::FLOAT:
      if (this->partial) {
        if (xp.asFloat() == yp.asFloat()) {
          continue;
        }
      } else {
        if (compareByTotalOrder(xp.asFloat(), yp.asFloat()) == 0) {
          continue;
        }
      }
      return false;
    case ValueKind::OBJECT:
      if (xp.get()->getKind() != yp.get()->getKind()) {
        return false;
      }
      if (reinterpret_cast<uintptr_t>(xp.get()) == reinterpret_cast<uintptr_t>(yp.get())) {
        continue; // fast path
      }
      break;
    default:
      return false;
    }

    // for object
    switch (xp.get()->getKind()) {
    case ObjectKind::Array: {
      auto &xa = typeAs<ArrayObject>(xp);
      auto &ya = typeAs<ArrayObject>(yp);
      if (xa.size() != ya.size()) {
        return false;
      }
      if (auto &frame = frames.back(); frame.xi < xa.size()) {
        GOTO_NEXT(frames, OrdFrame(xa.getValues()[frame.xi++], ya.getValues()[frame.yi++]));
      }
      continue;
    }
    case ObjectKind::OrderedMap: { // order-independent
      auto &xo = typeAs<OrderedMapObject>(xp);
      auto &yo = typeAs<OrderedMapObject>(yp);
      if (xo.size() != yo.size()) {
        return false;
      }
      auto &frame = frames.back();
      const unsigned int xi = skipEmptyEntries(xo, frame.xi);
      if (xi < xo.getEntries().getUsedSize()) {
        auto &xe = xo.getEntries()[xi];
        frame.xi++;
        const int yi = yo.lookup(xe.getKey());
        if (yi < 0) {
          return false;
        }
        GOTO_NEXT(frames, OrdFrame(xe.getValue(), yo.getEntries()[yi].getValue()));
      }
      continue;
    }
    case ObjectKind::Base: {
      auto &xo = typeAs<BaseObject>(xp);
      auto &yo = typeAs<BaseObject>(yp);
      if (xo.getFieldSize() != yo.getFieldSize()) {
        return false;
      }
      if (auto &frame = frames.back(); frame.xi < xo.getFieldSize()) {
        GOTO_NEXT(frames, OrdFrame(xo[frame.xi++], yo[frame.yi++]));
      }
      continue;
    }
    default:
      return false;
    }
  }
  }
  return true;
}

// ######################
// ##     Ordering     ##
// ######################

#define GOTO_NEXT2(FS, F)                                                                          \
  do {                                                                                             \
    (FS).push(F);                                                                                  \
    if ((FS).size() == STACK_DEPTH_LIMIT) {                                                        \
      this->overflow = true;                                                                       \
      return -1;                                                                                   \
    }                                                                                              \
    goto NEXT;                                                                                     \
  } while (false)

int Ordering::operator()(const Value &x, const Value &y) {
  this->overflow = false;
  InlinedStack<OrdFrame, 4> frames;
  for (frames.push(OrdFrame(x, y)); frames.size(); frames.pop()) {
  NEXT: {
    auto &xp = *frames.back().x;
    auto &yp = *frames.back().y;

    // for string
    if (xp.hasStrRef() && yp.hasStrRef()) {
      if (const int r = xp.asStrRef().compare(yp.asStrRef())) {
        return r;
      }
      continue;
    }

    if (xp.kind() != yp.kind()) {
      return toUnderlying(xp.kind()) - toUnderlying(yp.kind());
    }

    switch (xp.kind()) {
    case ValueKind::EMPTY:
    case ValueKind::INVALID:
      continue;
    case ValueKind::BOOL: {
      const int left = xp.asBool() ? 1 : 0;
      const int right = yp.asBool() ? 1 : 0;
      if (const int r = left - right) {
        return r;
      }
      continue;
    }
    case ValueKind::SIG: {
      if (const int r = xp.asSig() - yp.asSig()) {
        return r;
      }
      continue;
    }
    case ValueKind::INT: {
      const int64_t left = xp.asInt();
      const int64_t right = yp.asInt();
      if (left == right) {
        continue;
      }
      return left < right ? -1 : 1;
    }
    case ValueKind::FLOAT:
      if (const int r = compareByTotalOrder(xp.asFloat(), yp.asFloat())) {
        return r;
      }
      continue;
    case ValueKind::OBJECT:
      if (xp.get()->getKind() != yp.get()->getKind()) {
        return toUnderlying(xp.get()->getKind()) - toUnderlying(yp.get()->getKind());
      }
      if (reinterpret_cast<uintptr_t>(xp.get()) == reinterpret_cast<uintptr_t>(yp.get())) {
        continue; // fast path
      }
      break;
    default:
      return -1; // normally unreachable
    }

    // for object
    switch (xp.get()->getKind()) {
    case ObjectKind::Array: {
      auto &xa = typeAs<ArrayObject>(xp);
      auto &ya = typeAs<ArrayObject>(yp);
      const unsigned int xSize = xa.size();
      const unsigned int ySize = ya.size();
      if (auto &frame = frames.back(); frame.xi < xSize && frame.yi < ySize) {
        GOTO_NEXT2(frames, OrdFrame(xa.getValues()[frame.xi++], ya.getValues()[frame.yi++]));
      }
      if (xSize < ySize) {
        return -1;
      }
      if (xSize > ySize) {
        return 1;
      }
      continue;
    }
    case ObjectKind::OrderedMap: { // order-dependent
      auto &xo = typeAs<OrderedMapObject>(xp);
      auto &yo = typeAs<OrderedMapObject>(yp);
      auto &frame = frames.back();
      const unsigned int xi = skipEmptyEntries2(xo, frame.xi);
      const unsigned int yi = skipEmptyEntries2(yo, frame.yi);
      if (xi < xo.getEntries().getUsedSize() && yi < yo.getEntries().getUsedSize()) {
        const bool isKey = frame.xi % 2 == 0;
        frame.xi++;
        frame.yi++;
        auto &xe = xo.getEntries()[xi];
        auto &ye = yo.getEntries()[yi];
        GOTO_NEXT2(frames, OrdFrame(isKey ? xe.getKey() : xe.getValue(),
                                    isKey ? ye.getKey() : ye.getValue()));
      }
      if (xo.size() < yo.size()) {
        return -1;
      }
      if (xo.size() > yo.size()) {
        return 1;
      }
      continue;
    }
    case ObjectKind::Base: {
      auto &xo = typeAs<BaseObject>(xp);
      auto &yo = typeAs<BaseObject>(yp);
      const unsigned int xSize = xo.getFieldSize();
      const unsigned int ySize = yo.getFieldSize();
      if (auto &frame = frames.back(); frame.xi < xSize && frame.yi < ySize) {
        GOTO_NEXT2(frames, OrdFrame(xo[frame.xi++], yo[frame.yi++]));
      }
      if (xSize < ySize) {
        return -1;
      }
      if (xSize > ySize) {
        return 1;
      }
      continue;
    }
    default: // normally unreachable
      return static_cast<int>(reinterpret_cast<intptr_t>(xp.get()) -
                              reinterpret_cast<intptr_t>(yp.get()));
    }
  }
  }
  return 0;
}

// #########################
// ##     Stringifier     ##
// #########################

static std::string toString(double value) {
  if (std::isnan(value)) {
    return "NaN";
  }
  if (std::isinf(value)) {
    return value > 0 ? "Infinity" : "-Infinity";
  }
  Decimal decimal{};
  Decimal::create(value, decimal);
  return decimal.toString();
}

bool Stringifier::addAsFlatStr(const Value &value) {
  switch (value.kind()) {
  case ValueKind::EMPTY:
    return true; // do nothing
  case ValueKind::NUMBER: {
    auto v = std::to_string(value.asNum());
    return this->appender(v);
  }
  case ValueKind::NUM_LIST:
  case ValueKind::STACK_GUARD: {
    auto &nums = value.asNumList();
    char buf[128];
    const int s = snprintf(buf, std::size(buf), "[%u, %u, %u]", nums[0], nums[1], nums[2]);
    assert(s > 0);
    return this->appender(StringRef(buf, s));
  }
  case ValueKind::DUMMY: {
    const unsigned int typeId = value.asTypeId();
    if (typeId == toUnderlying(TYPE::Module)) { // for temporary module descriptor
      char buf[64];
      const int s =
          snprintf(buf, std::size(buf), "%s%u)", OBJ_TEMP_MOD_PREFIX, value.asNumList()[1]);
      assert(s > 0);
      return this->appender(StringRef(buf, s));
    }
    char buf[64];
    const int s = snprintf(buf, std::size(buf), "Object(%u)", typeId);
    assert(s > 0);
    return this->appender(StringRef(buf, s));
  }
  case ValueKind::EXPAND_META:
    return this->appender(toString(value.asExpandMeta().first));
  case ValueKind::INVALID:
    return this->appender("(invalid)");
  case ValueKind::BOOL:
    return this->appender(value.asBool() ? "true" : "false");
  case ValueKind::SIG: {
    auto v = std::to_string(value.asSig());
    return this->appender(v);
  }
  case ValueKind::INT: {
    auto v = std::to_string(value.asInt());
    return this->appender(v);
  }
  case ValueKind::FLOAT: {
    double d = value.asFloat();
    auto v = toString(d);
    return this->appender(v);
  }
  default:
    if (value.hasStrRef()) {
      return this->appender(value.asStrRef());
    }
    assert(value.kind() == ValueKind::OBJECT);
    break;
  }

  // for non-nested object
  switch (value.get()->getKind()) {
  case ObjectKind::UnixFd: {
    auto str = typeAs<UnixFdObject>(value).toString();
    return this->appender(str);
  }
  case ObjectKind::Regex:
    return this->appender(typeAs<RegexObject>(value).getStr());
  case ObjectKind::Error: {
    auto &obj = typeAs<ErrorObject>(value);
    const auto ref = obj.getMessage().asStrRef();
    return this->appender(this->pool.get(obj.getTypeID()).getNameRef()) && this->appender(": ") &&
           this->appender(ref);
  }
  case ObjectKind::Func: {
    auto &obj = typeAs<FuncObject>(value);
    std::string str;
    const auto kind = obj.getCode().getKind();
    switch (kind) {
    case CodeKind::TOPLEVEL:
      str += OBJ_MOD_PREFIX;
      break;
    case CodeKind::FUNCTION:
      str += "function(";
      break;
    case CodeKind::USER_DEFINED_CMD:
      str += "command(";
      break;
    default:
      break;
    }
    const auto modId = obj.getCode().getBelongedModId();
    if (const char *name = obj.getCode().getName(); name[0]) {
      if (!isBuiltinMod(modId) &&
          (kind == CodeKind::FUNCTION || kind == CodeKind::USER_DEFINED_CMD)) { // NOLINT
        str += toModTypeName(modId);
        str += '.';
      }
      str += name;
    } else {
      char buf[32]; // hex of 64bit pointer is up to 16 chars
      snprintf(buf, std::size(buf), "0x%zx", reinterpret_cast<uintptr_t>(&obj));
      str += buf;
    }
    str += ")";
    return this->appender(str);
  }
  case ObjectKind::Closure: {
    char buf[32]; // hex of 64bit pointer is up to 16 chars
    const int s = snprintf(buf, std::size(buf), "closure(0x%zx)",
                           reinterpret_cast<uintptr_t>(&typeAs<ClosureObject>(value).getFuncObj()));
    assert(s > 0);
    return this->appender(StringRef(buf, s));
  }
  case ObjectKind::Job: {
    char buf[32];
    const int s = snprintf(buf, std::size(buf), "%%%u", typeAs<JobObject>(value).getJobID());
    assert(s > 0);
    return this->appender(StringRef(buf, s));
  }
  case ObjectKind::Candidate: {
    auto &obj = typeAs<CandidateObject>(value);
    return this->appender(obj.underlying());
  }
  default:
    char buf[32]; // hex of 64bit pointer is up to 16 chars
    const int s =
        snprintf(buf, std::size(buf), "Object(0x%zx)", reinterpret_cast<uintptr_t>(value.get()));
    assert(s > 0);
    return this->appender(StringRef(buf, s));
  }
}

struct StrFrame {
  const Value *v;
  unsigned int i;
  unsigned int p; // for packed field names

  StrFrame() = default;

  explicit StrFrame(const Value &v) : v(&v), i(0), p(0) {}
};

#define TRY(E)                                                                                     \
  do {                                                                                             \
    if (unlikely(!(E))) {                                                                          \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

static StringRef nextFieldName(const char *packedFieldNames, unsigned int &index) {
  const unsigned int start = index;
  for (; packedFieldNames[index] && packedFieldNames[index] != ';'; index++)
    ;
  StringRef ref(packedFieldNames + start, index - start);
  if (packedFieldNames[index] == ';') {
    index++;
  }
  return ref;
}

bool Stringifier::addAsStr(const Value &value) {
  this->overflow = false;
  InlinedStack<StrFrame, 4> frames;
  for (frames.push(StrFrame(value)); frames.size(); frames.pop()) {
  NEXT: {
    const auto &v = *frames.back().v;
    if (v.isObject()) {
      switch (v.get()->getKind()) {
      case ObjectKind::RegexMatch:
      case ObjectKind::Array: {
        const auto &values = isa<ArrayObject>(v.get()) ? typeAs<ArrayObject>(v).getValues()
                                                       : typeAs<RegexMatchObject>(v).getGroups();
        if (values.empty()) { // for empty
          TRY(this->appender("[]"));
          continue;
        }
        if (auto &frame = frames.back(); frame.i < values.size()) {
          TRY(this->appender(frame.i == 0 ? "[" : ", "));
          GOTO_NEXT(frames, StrFrame(values[frame.i++]));
        }
        TRY(this->appender("]"));
        continue;
      }
      case ObjectKind::OrderedMap: {
        auto &obj = typeAs<OrderedMapObject>(v);
        if (obj.size() == 0) { // for empty
          TRY(this->appender("[]"));
          continue;
        }
        auto &frame = frames.back();
        const bool first = frame.i == 0;
        if (const unsigned int index = skipEmptyEntries2(obj, frame.i);
            index < obj.getEntries().getUsedSize()) {
          const bool isKey = frame.i % 2 == 0;
          const char *cc = first ? "[" : isKey ? ", " : " : ";
          TRY(this->appender(cc));
          frame.i++;
          auto &e = obj.getEntries()[index];
          GOTO_NEXT(frames, StrFrame(isKey ? e.getKey() : e.getValue()));
        }
        TRY(this->appender("]"));
        continue;
      }
      case ObjectKind::Base: {
        auto &frame = frames.back();
        auto &obj = typeAs<BaseObject>(v);
        if (auto &type = this->pool.get(obj.getTypeID()); type.isRecordOrDerived()) {
          if (obj.getFieldSize() == 0) {
            TRY(this->appender("{}"));
            continue;
          }
          if (frame.i < obj.getFieldSize()) {
            TRY(this->appender(frame.i == 0 ? "{" : ", ") &&
                this->appender(
                    nextFieldName(cast<RecordType>(type).getPackedFieldNames(), frame.p)) &&
                this->appender(" : "));
            GOTO_NEXT(frames, StrFrame(obj[frame.i++]));
          }
          TRY(this->appender("}"));
        } else {
          if (frame.i < obj.getFieldSize()) {
            TRY(this->appender(frame.i == 0 ? "(" : ", "));
            GOTO_NEXT(frames, StrFrame(obj[frame.i++]));
          }
          TRY(this->appender(obj.getFieldSize() == 1 ? ",)" : ")"));
        }
        continue;
      }
      default:
        break;
      }
    }
    TRY(this->addAsFlatStr(v));
  }
  }
  return true;
}

bool Stringifier::addAsInterp(const Value &value) {
  this->overflow = false;
  InlinedStack<StrFrame, 4> frames;
  for (frames.push(StrFrame(value)); frames.size(); frames.pop()) {
  NEXT: {
    const auto &v = *frames.back().v;
    if (v.isObject()) {
      switch (v.get()->getKind()) {
      case ObjectKind::RegexMatch:
      case ObjectKind::Array: {
        const auto &values = isa<ArrayObject>(v.get()) ? typeAs<ArrayObject>(v).getValues()
                                                       : typeAs<RegexMatchObject>(v).getGroups();
        auto &frame = frames.back();
        for (; frame.i < values.size() && values[frame.i].isInvalid(); frame.i++)
          ;
        if (frame.i < values.size()) {
          if (frame.p++ > 0) {
            TRY(this->appender(" "));
          }
          GOTO_NEXT(frames, StrFrame(values[frame.i++]));
        }
        continue;
      }
      case ObjectKind::OrderedMap: {
        auto &obj = typeAs<OrderedMapObject>(v);
        auto &frame = frames.back();
        unsigned int index = skipEmptyEntries2(obj, frame.i);
        while (index < obj.getEntries().getUsedSize() &&
               obj.getEntries()[index].getValue().isInvalid()) {
          frame.i += 2;
          index = skipEmptyEntries2(obj, frame.i);
        }
        if (index < obj.getEntries().getUsedSize()) {
          const bool isKey = frame.i % 2 == 0;
          frame.i++;
          if (frame.p++ > 0) {
            TRY(this->appender(" "));
          }
          auto &e = obj.getEntries()[index];
          GOTO_NEXT(frames, StrFrame(isKey ? e.getKey() : e.getValue()));
        }
        continue;
      }
      case ObjectKind::Base: {
        auto &obj = typeAs<BaseObject>(v);
        assert(this->pool.get(obj.getTypeID()).isTupleType() ||
               this->pool.get(obj.getTypeID()).isRecordOrDerived());
        auto &frame = frames.back();
        for (; frame.i < obj.getFieldSize() && obj[frame.i].isInvalid(); frame.i++)
          ;
        if (frame.i < obj.getFieldSize()) {
          if (frame.p++ > 0) {
            TRY(this->appender(" "));
          }
          GOTO_NEXT(frames, StrFrame(obj[frame.i++]));
        }
        continue;
      }
      default:
        break;
      }
    }
    TRY(this->addAsFlatStr(v));
  }
  }
  return true;
}

bool StrAppender::operator()(const StringRef ref) {
  switch (this->appendOp) {
  case Op::APPEND:
    return checkedAppend(ref, this->limit, this->buf);
  case Op::PRINTABLE:
    return appendAsPrintable(ref, this->limit, this->buf);
  }
  return true; // normally unreachable
}

bool StrObjAppender::operator()(StringRef ref) { return this->value.appendAsStr(this->state, ref); }

#define GOTO_NEXT3(FS, F)                                                                          \
  do {                                                                                             \
    (FS).push(F);                                                                                  \
    if ((FS).size() == SYS_LIMIT_NESTED_OBJ_DEPTH) {                                               \
      overflow = true;                                                                             \
      goto END;                                                                                    \
    }                                                                                              \
    goto NEXT;                                                                                     \
  } while (false)

bool addAsCmdArg(ARState &state, Value &&value, ArrayObject &argv, Value &redir) {
  if (value.hasStrRef()) { // fast path
    return argv.append(state, std::move(value));
  }

  bool overflow = false;
  InlinedStack<StrFrame, 4> frames;
  for (frames.push(StrFrame(value)); frames.size(); frames.pop()) {
  NEXT: {
    const auto &arg = *frames.back().v;
    if (arg.isInvalid()) {
      continue;
    }
    if (arg.hasStrRef()) { // fast path
      TRY(argv.append(state, Value(arg)));
      continue;
    }
    if (arg.isObject()) {
      switch (arg.get()->getKind()) {
      case ObjectKind::UnixFd: {
        /**
         * when pass fd object to command arguments,
         * unset close-on-exec and add pseudo redirection entry
         */
        if (!redir) {
          redir = Value::create<RedirObject>();
        } else if (redir.isInvalid()) {
          raiseError(state, TYPE::ArgumentError, "cannot pass FD object to argument array");
          return false;
        }

        const auto orgRefCount = arg.get()->getRefcount(); // if 1, arg == value
        auto fdObj = toObjPtr<UnixFdObject>(arg);
        if (orgRefCount > 1) {
          if (auto ret = fdObj->dupWithCloseOnExec()) {
            fdObj = std::move(ret);
          } else {
            raiseSystemError(state, errno, "failed to pass FD object to command arguments");
            return false;
          }
        }
        if (argv.append(state, Value::createStr(fdObj->toString()))) {
          assert(fdObj->getRefcount() == 1 || arg.get() == fdObj.get());
          if (!fdObj->closeOnExec(false)) {
            raiseSystemError(state, errno, "failed to pass FD object to command arguments");
            return false;
          }
          typeAs<RedirObject>(redir).addEntry(fdObj, RedirOp::NOP, -1);
          continue;
        }
        return false;
      }
      case ObjectKind::RegexMatch:
      case ObjectKind::Array: {
        auto &values = isa<ArrayObject>(arg.get()) ? typeAs<ArrayObject>(arg).getValues()
                                                   : typeAs<RegexMatchObject>(arg).getGroups();
        if (auto &frame = frames.back(); frame.i < values.size()) {
          GOTO_NEXT3(frames, StrFrame(values[frame.i++]));
        }
        continue;
      }
      case ObjectKind::OrderedMap: {
        auto &obj = typeAs<OrderedMapObject>(arg);
        auto &frame = frames.back();
        unsigned int index = skipEmptyEntries2(obj, frame.i);
        while (index < obj.getEntries().getUsedSize() &&
               obj.getEntries()[index].getValue().isInvalid()) {
          frame.i += 2;
          index = skipEmptyEntries2(obj, frame.i);
        }
        if (index < obj.getEntries().getUsedSize()) {
          const bool isKey = frame.i % 2 == 0;
          frame.i++;
          auto &e = obj.getEntries()[index];
          GOTO_NEXT3(frames, StrFrame(isKey ? e.getKey() : e.getValue()));
        }
        continue;
      }
      case ObjectKind::Base: {
        auto &obj = typeAs<BaseObject>(arg);
        if (auto &frame = frames.back(); frame.i < obj.getFieldSize()) {
          GOTO_NEXT3(frames, StrFrame(obj[frame.i++]));
        }
        continue;
      }
      default:
        break;
      }
    }
    auto v = Value::createStr();
    TRY(arg.opStr(state, v) && argv.append(state, std::move(v)));
  }
  }
END:
  if (overflow) {
    raiseError(state, TYPE::StackOverflowError,
               "pass deep nesting object to command argument list");
    return false;
  }
  return true;
}

} // namespace arsh