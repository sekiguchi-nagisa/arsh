/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#ifndef YDSH_MISC_QUEUE_HPP
#define YDSH_MISC_QUEUE_HPP

#include <type_traits>

namespace ydsh {

template <typename T, unsigned int N>
class FixedQueue {
private:
    T data_[N];
    unsigned int size_;

    static_assert(std::is_pod<T>::value, "forbidden type");

public:
    FixedQueue() noexcept : data_(), size_(0) {}
    ~FixedQueue() = default;

    unsigned int size() const {
        return this->size_;
    }

    bool empty() const {
        return this->size() == 0;
    }

    bool full() const {
        return this->size() == N;
    }

    bool push(const T &v) {
        if(!this->full()) {
            this->data_[this->size_++] = v;
            return true;
        }
        return false;
    }

    void clear() {
        this->size_ = 0;
    }

    /**
     * call when !empty()
     * @return
     */
    T pop() {
        T v = this->data_[0];
        this->moveToFirst();
        return v;
    }

private:
    void moveToFirst();
};

template <typename T, unsigned int N>
void FixedQueue<T, N>::moveToFirst() {
    for(unsigned int i = 1; i < this->size(); i++) {
        this->data_[i - 1] = this->data_[i];
    }
    --this->size_;
}

} // namespace ydsh

#endif //YDSH_MISC_QUEUE_HPP
