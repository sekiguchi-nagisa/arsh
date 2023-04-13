/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#include "brace.h"
#include "constant.h"
#include "misc/format.hpp"
#include "misc/num_util.hpp"

namespace ydsh {

struct BraceInt {
  int64_t value;
  unsigned int digits; // if no-padding, it is 0
  bool ok;

  explicit operator bool() const { return this->ok; }
};

static BraceInt toBraceInt(StringRef ref) {
  assert(!ref.empty());

  constexpr uint64_t LIMIT = static_cast<uint64_t>(INT64_MAX) + 1;
  bool negate = false;
  if (ref[0] == '+') {
    ref.removePrefix(1);
  } else if (ref[0] == '-') {
    ref.removePrefix(1);
    negate = true;
  }
  unsigned int digits = 0;
  while (ref.size() > 1 && ref[0] == '0') {
    digits++;
    ref.removePrefix(1);
  }

  bool ok = false;
  int64_t value = 0;
  auto ret = convertToDecimal<uint64_t>(ref.begin(), ref.end());
  if (ret.second) {
    if (negate) {
      if (ret.first < LIMIT) {
        ok = true;
        value = -1 * static_cast<int64_t>(ret.first);
      } else if (ret.first == LIMIT) {
        ok = true;
        value = INT64_MIN;
      }
    } else if (ret.first < LIMIT) {
      ok = true;
      value = static_cast<int64_t>(ret.first);
    }
  }

  if (ok && digits > 0) { // resolve digits
    auto v = static_cast<uint64_t>(value == INT64_MIN ? INT64_MAX : std::abs(value));
    digits += countDigits(v) + (negate ? 1 : 0);
  }

  return BraceInt{
      .value = value,
      .digits = digits,
      .ok = ok,
  };
}

static int64_t toReversedBegin(int64_t begin, int64_t end, int64_t step) {
  static_assert(__SIZEOF_INT128__ == 16);
  __int128 diff = static_cast<__int128>(end) - static_cast<__int128>(begin);
  __int128 n = diff / static_cast<__int128>(step);
  __int128 value = static_cast<__int128>(begin) + n * static_cast<__int128>(step);
  assert(value >= INT64_MIN && value <= INT64_MAX);
  return static_cast<int64_t>(value);
}

BraceRange toBraceRange(StringRef ref, bool isChar, std::string &error) {
  BraceRange braceRange = {
      .begin = 0,
      .end = 0,
      .step = 1,
      .digits = 0,
      .kind = isChar ? BraceRange::Kind::CHAR : BraceRange::Kind::INT,
  };

  // split by '..'
  auto pos = ref.find("..");
  assert(pos != StringRef::npos);
  auto begin = ref.slice(0, pos);
  auto end = ref.substr(pos + 2);
  StringRef step;
  pos = end.find("..");
  if (pos != StringRef::npos) {
    step = end.substr(pos + 2);
    end = end.slice(0, pos);
  }

  // parse begin..end
  if (isChar) {
    assert(begin.size() == 1);
    assert(end.size() == 1);
    braceRange.begin = static_cast<unsigned char>(begin[0]);
    braceRange.end = static_cast<unsigned char>(end[0]);
  } else {
    auto ret1 = toBraceInt(begin);
    if (!ret1) {
      braceRange.kind = BraceRange::Kind::OUT_OF_RANGE;
      error = begin.toString();
      goto END;
    }
    auto ret2 = toBraceInt(end);
    if (!ret2) {
      braceRange.kind = BraceRange::Kind::OUT_OF_RANGE;
      error = end.toString();
      goto END;
    }
    braceRange.begin = ret1.value;
    braceRange.end = ret2.value;

    // resolve digits
    if (ret1.digits && !ret2.digits) {
      ret2.digits = std::to_string(ret2.value).size();
    } else if (!ret1.digits && ret2.digits) {
      ret1.digits = std::to_string(ret1.value).size();
    }
    braceRange.digits = std::max(ret1.digits, ret2.digits);
  }

  if (!step.empty()) {
    auto ret = toBraceInt(step);
    if (!ret) {
      braceRange.kind = BraceRange::Kind::OUT_OF_RANGE_STEP;
      error = step.toString();
      goto END;
    }
    int64_t v = ret.value;
    switch (v) {
    case 0:
      v = 1; // if step 0, treat as 1
      break;
    case INT64_MIN:
      braceRange.kind = BraceRange::Kind::OUT_OF_RANGE_STEP;
      error = step.toString();
      goto END;
    default:
      if (v < 0) {
        v = std::abs(v);
        auto r = toReversedBegin(braceRange.begin, braceRange.end, v);
        braceRange.end = braceRange.begin;
        braceRange.begin = r;
      }
      break;
    }
    braceRange.step = v;
  }

END:
  return braceRange;
}

std::string formatSeqValue(int64_t v, unsigned int digits, bool isChar) {
  std::string value;
  if (isChar) {
    value += static_cast<char>(v);
  } else {
    value = std::to_string(v);
    if (digits) {
      assert(value.size() <= digits);
      std::string tmp;
      tmp.resize(digits - value.size(), '0');
      value.insert(v < 0 ? 1 : 0, tmp);
    }
  }
  return value;
}

bool tryUpdateSeqValue(int64_t &cur, const BraceRange &range) {
  assert(range.step > 0);

  const bool inc = range.begin <= range.end;
  if (inc) {
    int64_t ret;
    if (unlikely(sadd_overflow(cur, range.step, ret))) {
      return false;
    }
    if (ret > range.end) {
      return false;
    }
    cur = ret;
  } else {
    int64_t ret;
    if (unlikely(ssub_overflow(cur, range.step, ret))) {
      return false;
    }
    if (ret < range.end) {
      return false;
    }
    cur = ret;
  }
  return true;
}

} // namespace ydsh