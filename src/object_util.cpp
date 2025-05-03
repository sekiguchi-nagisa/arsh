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
#include "misc/inlined_stack.hpp"
#include "misc/num_util.hpp"
#include "object.h"

namespace arsh {

// ######################
// ##     Equality     ##
// ######################

struct OrdFrame {
  const Value *x;
  const Value *y;

  static OrdFrame create(const Value &x, const Value &y) {
    OrdFrame frame;
    frame.x = &x;
    frame.y = &y;
    return frame;
  }
};

bool Equality::operator()(const Value &x, const Value &y) {
  InlinedStack<OrdFrame, 4> frames;
  for (frames.push(OrdFrame::create(x, y)); frames.size(); frames.pop()) {
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
    case ObjectKind::Array:
    case ObjectKind::OrderedMap:
    case ObjectKind::Base:
      return false; // TODO:
    default:
      return false;
    }
  }
  return true;
}

// ######################
// ##     Ordering     ##
// ######################

int Ordering::operator()(const Value &x, const Value &y) {
  InlinedStack<OrdFrame, 4> frames;
  for (frames.push(OrdFrame::create(x, y)); frames.size(); frames.pop()) {
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
      if (xp.isInvalid()) {
        return -1;
      }
      if (yp.isInvalid()) {
        return 1;
      }
      return -1;
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
        return -1;
      }
      break;
    default:
      return -1; // normally unreachable
    }

    // for object
    switch (xp.get()->getKind()) {
    case ObjectKind::Array:
    case ObjectKind::OrderedMap:
    case ObjectKind::Base:
      return -1; // TODO:
    default:
      return -1; // normally unreachable
    }
  }
  return 0;
}

} // namespace arsh