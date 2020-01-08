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

#ifndef YDSH_MISC_STRING_REF_HPP
#define YDSH_MISC_STRING_REF_HPP

#include <string>
#include <cstring>
#include <cassert>

#include "hash.hpp"

namespace ydsh {

/**
 * similar to std::string_view/llvm::StringRef
 */
template <bool T>
class StringRefBase {
public:
    static_assert(T, "not allow instantiation");

    using size_type = std::size_t;

    static constexpr const size_type npos = -1;

private:
    const char *ptr_{nullptr};
    size_type size_{0};

public:
    StringRefBase() noexcept : ptr_(nullptr), size_(0) {}

    StringRefBase(const StringRefBase &ref) noexcept = default;

    /**
     *
     * @param value
     * may be null. if not null, must be null-terminated string.
     */
    StringRefBase(const char *value) : ptr_(value), size_(0) { //NOLINT
        if(this->ptr_) {
            this->size_ = strlen(this->ptr_);
        }
    }

    StringRefBase(const char *value, size_type size) noexcept : ptr_(value), size_(size) {}

    StringRefBase(const std::string &value) noexcept : ptr_(value.c_str()), size_(value.size()) {} //NOLINT

    size_type size() const {
        return this->size_;
    }

    bool empty() const {
        return this->size() == 0;
    }

    const char *data() const {
        return this->ptr_;
    }

    const char *take() {
        const char *tmp = nullptr;
        std::swap(this->ptr_, tmp);
        this->size_ = 0;
        return tmp;
    }

    int compare(StringRefBase ref) const noexcept {
        size_t size = std::min(this->size_, ref.size_);
        int ret = memcmp(this->ptr_, ref.ptr_, size);
        if(ret) {
            return ret;
        }
        if(this->size_ < ref.size_) {
            return -1;
        }
        return this->size_ == ref.size_ ? 0 : 1;
    }

    const char *begin() const {
        return this->ptr_;
    }

    const char *end() const {
        return this->ptr_ + this->size_;
    }

    char operator[](size_type index) const {
        assert(index < this->size_);
        return this->ptr_[index];
    }

    char front() const {
        return (*this)[0];
    }

    char back() const {
        return (*this)[this->size_ - 1];
    }

    StringRefBase substr(size_type pos = 0, size_type size = npos) const {
        assert(pos < this->size());
        size = std::min(this->size() - pos, size);
        return StringRefBase(this->data() + pos, size);
    }

    /**
     *
     * @param startIndex
     * inclusive
     * @param stopIndex
     * exclusive
     * @return
     */
    StringRefBase slice(size_type startIndex, size_type stopIndex) const {
        assert(startIndex <= stopIndex);
        assert(startIndex < this->size_);
        return this->substr(startIndex, stopIndex - startIndex);
    }

    size_type find(StringRefBase ref, size_type pos = 0) const {
        if(pos > this->size_) {
            return npos;
        }
        if(ref.size_ == 0) {
            return pos;
        }
        auto *ret = memmem(this->ptr_ + pos, this->size_ - pos, ref.ptr_, ref.size_);
        return ret != nullptr ? static_cast<const char *>(ret) - this->ptr_ : npos;
    }

    size_type indexOf(StringRefBase ref) const {
        return this->find(ref, 0);
    }

    size_type lastIndexOf(StringRefBase ref) const {
        size_type ret = npos;
        size_type pos = 0;
        do {
            auto tmp = this->find(ref, pos);
            if(tmp == npos) {
                break;
            }
            ret = tmp;
        } while(++pos != this->size_);
        return ret;
    }

    bool startsWith(StringRefBase ref) const {
        return this->size_ >= ref.size_ && memcmp(this->ptr_, ref.ptr_, ref.size_) == 0;
    }

    bool endsWith(StringRefBase ref) const {
        return this->size_ >= ref.size_ &&
            memcmp(this->ptr_ + (this->size_ - ref.size_), ref.ptr_, ref.size_) == 0;
    }
};

template <bool T>
constexpr const typename StringRefBase<T>::size_type StringRefBase<T>::npos;

using StringRef = StringRefBase<true>;

inline bool operator==(StringRef left, StringRef right) {
    return left.size() == right.size() && memcmp(left.data(), right.data(), left.size()) == 0;
}

inline bool operator!=(StringRef left, StringRef right) {
    return !(left == right);
}

inline bool operator<(StringRef left, StringRef right) {
    return left.compare(right) < 0;
}

inline bool operator>(StringRef left, StringRef right) {
    return left.compare(right) > 0;
}

inline bool operator<=(StringRef left, StringRef right) {
    return left.compare(right) <= 0;
}

inline bool operator>=(StringRef left, StringRef right) {
    return left.compare(right) >= 0;
}

} // namespace ydsh

namespace std {

template <>
struct hash<ydsh::StringRef> {
    std::size_t operator()(const ydsh::StringRef &ref) const {
        return ydsh::FNVHash64::compute(ref.begin(), ref.end());
    }
};

} // namespace std

#endif //YDSH_MISC_STRING_REF_HPP
