/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef MISC_LIB_EMITTER_HPP
#define MISC_LIB_EMITTER_HPP

#include <unordered_map>
#include <vector>

#include "buffer.hpp"
#include "resource.hpp"

BEGIN_MISC_LIB_NAMESPACE_DECL

class LabelImpl : public RefCount<LabelImpl> {
private:
  /**
   * indicate labeled address
   */
  unsigned int index{0};

  ~LabelImpl() = default;

  friend struct RefCountOp<LabelImpl>;

public:
  bool operator==(const LabelImpl &l) const noexcept {
    return reinterpret_cast<uintptr_t>(this) == reinterpret_cast<uintptr_t>(&l);
  }

  void setIndex(unsigned int i) { this->index = i; }

  unsigned int getIndex() const { return this->index; }
};

using Label = IntrusivePtr<LabelImpl>;

inline Label makeLabel() { return Label::create(); }

using CodeBuffer = FlexBuffer<unsigned char>;

template <bool T>
struct CodeEmitter {
  static_assert(true, "not allow instantiation");

  struct Compare {
    bool operator()(const Label &x, const Label &y) const noexcept { return *x == *y; }
  };

  struct GenHash {
    std::size_t operator()(const Label &l) const noexcept {
      return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(l.get()));
    }
  };

  struct LabelTarget {
    unsigned int targetIndex;
    unsigned int baseIndex;
    enum OffsetLen {
      _8,
      _16,
      _32,
    };

    OffsetLen offsetLen;
  };

  std::unordered_map<Label, std::vector<LabelTarget>, GenHash, Compare> labelMap;

  CodeBuffer codeBuffer;

  void emit8(unsigned int index, unsigned char b) noexcept {
    write8(this->codeBuffer.begin() + index, b);
  }

  void emit16(unsigned int index, unsigned short b) noexcept {
    write16(this->codeBuffer.begin() + index, b);
  }

  void emit24(unsigned int index, unsigned int b) noexcept {
    write24(this->codeBuffer.begin() + index, b);
  }

  void emit32(unsigned int index, unsigned int b) noexcept {
    write32(this->codeBuffer.begin() + index, b);
  }

  void emit64(unsigned int index, uint64_t b) noexcept {
    write64(this->codeBuffer.begin() + index, b);
  }

  void emit(unsigned int index, uint64_t b) noexcept {
    if (b <= UINT8_MAX) {
      this->emit8(index, static_cast<unsigned char>(b));
    } else if (b <= UINT16_MAX) {
      this->emit16(index, static_cast<unsigned short>(b));
    } else if (b <= UINT32_MAX) {
      this->emit32(index, static_cast<unsigned int>(b));
    } else {
      this->emit64(index, b);
    }
  }

  void append8(unsigned char b) {
    const unsigned int index = this->codeBuffer.size();
    this->codeBuffer.resize(index + 1, 0);
    this->emit8(index, b);
  }

  void append16(unsigned short b) {
    const unsigned int index = this->codeBuffer.size();
    this->codeBuffer.resize(index + 2, 0);
    this->emit16(index, b);
  }

  void append24(unsigned int b) {
    const unsigned int index = this->codeBuffer.size();
    this->codeBuffer.resize(index + 3, 0);
    this->emit24(index, b);
  }

  void append32(unsigned int b) {
    const unsigned int index = this->codeBuffer.size();
    this->codeBuffer.resize(index + 4, 0);
    this->emit32(index, b);
  }

  void append64(uint64_t b) {
    const unsigned int index = this->codeBuffer.size();
    this->codeBuffer.resize(index + 8, 0);
    this->emit64(index, b);
  }

  void append(uint64_t b) {
    if (b <= UINT8_MAX) {
      this->append8(static_cast<unsigned char>(b));
    } else if (b <= UINT16_MAX) {
      this->append16(static_cast<unsigned short>(b));
    } else if (b <= UINT32_MAX) {
      this->append32(static_cast<unsigned int>(b));
    } else {
      this->append64(b);
    }
  }

  void markLabel(unsigned int index, Label &label) {
    label->setIndex(index);
    this->labelMap[label];
  }

  void writeLabel(unsigned int index, const Label &label, unsigned int baseIndex,
                  typename LabelTarget::OffsetLen len) {
    this->labelMap[label].push_back({index, baseIndex, len});
  }

  bool finalize() {
    // write labeled index.
    for (auto &e : this->labelMap) {
      const unsigned int location = e.first->getIndex();
      for (auto &u : e.second) {
        const unsigned int targetIndex = u.targetIndex;
        if (u.baseIndex > location) {
          fprintf(stderr, "base index: %u, label location: %u\n", u.baseIndex, location);
          return false;
        }
        const unsigned int offset = location - u.baseIndex;
        switch (u.offsetLen) {
        case LabelTarget::_8:
          if (offset > UINT8_MAX) {
            fprintf(stderr, "offset is greater than UINT8_MAX\n");
            return false;
          }
          this->emit8(targetIndex, offset);
          break;
        case LabelTarget::_16:
          if (offset > UINT16_MAX) {
            fprintf(stderr, "offset is greater than UINT16_MAX\n");
            return false;
          }
          this->emit16(targetIndex, offset);
          break;
        case LabelTarget::_32:
          this->emit32(targetIndex, offset);
          break;
        }
      }
    }
    this->labelMap.clear();
    return true;
  }
};

END_MISC_LIB_NAMESPACE_DECL

#endif // MISC_LIB_EMITTER_HPP
