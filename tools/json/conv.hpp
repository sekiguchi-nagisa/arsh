/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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
#ifndef YDSH_TOOLS_CONV_HPP
#define YDSH_TOOLS_CONV_HPP

#include "json.h"

namespace ydsh {
namespace rpc {

using namespace json;

// helper function for json conversion
inline void fromJSON(JSON &&json, std::string &value) {
    value = std::move(json.asString());
}

template <typename T>
void fromJSON(JSON &&json, std::vector<T> &value) {
    value.reserve(json.asArray().size());
    for(auto &e : json.asArray()) {
        T v;
        fromJSON(std::move(e), v);
        value.push_back(std::move(v));
    }
}

inline JSON toJSON(const std::string &str) {
    return JSON(str);
}

inline JSON toJSON(int value) {
    return JSON(value);
}

inline JSON toJSON(std::nullptr_t) {
    return JSON(nullptr);
}

inline JSON toJSON(const JSON &value) {
    return JSON(value);
}

template <typename T>
JSON toJSON(const std::vector<T> &value) {
    json::Array jsonArray(value.size());
    for(unsigned int i = 0; i < value.size(); i++) {
        jsonArray[i] = toJSON(value[i]);
    }
    return JSON(std::move(jsonArray));
}

namespace __detail {

template <int N, typename ...R>
struct ToJSON {
    JSON operator()(const Union<R...> &value) const {
        if(value.tag() == N) {
            using T = typename TypeByIndex<N, R...>::type;
            return toJSON(get<T>(value));
        } else {
            return ToJSON<N - 1, R...>()(value);
        }
    }
};

template <typename ...R>
struct ToJSON<-1, R...> {
    JSON operator()(const Union<R...> &) const {
        return JSON();
    }
};

} // namespace __detail

template <typename ...R>
JSON toJSON(const Union<R...> &value) {
    return __detail::ToJSON<sizeof...(R) - 1, R...>()(value);
}

} // namespace rpc
} // namespace ydsh


#endif //YDSH_TOOLS_CONV_HPP
