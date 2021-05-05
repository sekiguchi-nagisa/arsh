/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_SERIALIZE_H
#define YDSH_TOOLS_SERIALIZE_H

#include "json.h"

namespace ydsh {
namespace json {

template <typename T> struct is_array { static constexpr bool value = false; };

template <typename T> struct is_array<std::vector<T>> { static constexpr bool value = true; };

template <typename T>
static constexpr bool is_array_v = is_array<T>::value;

template <typename T>
static constexpr bool is_string_v = std::is_same_v<T, String>;

template <typename T> struct is_union { static constexpr bool value = false; };

template <typename ...R> struct is_union<Union<R...>> { static constexpr bool value = true; };

template <typename T>
static constexpr bool is_union_v = is_union<T>::value;

template <typename T>
static constexpr bool is_object_v =
        !is_string_v<T> && !is_array_v<T> && !is_union_v<T> && std::is_class_v<T>;

template <typename T> struct array_element {};

template <typename T> struct array_element<std::vector<T>> { using type = T; };

template <typename T>
using array_element_t = typename array_element<T>::type;


template <typename S, typename T>
void jsonify(S &s, T &v) {
    v.jsonify(s);
}

class JSONSerializer {
private:
    /**
     * final serialized value
     */
    JSON result;

    static JSONSerializer asArray();

    static JSONSerializer asObject();

public:
    const JSON &get() const {
        return this->result;
    }

    JSON take() && {
        return std::move(result);
    }

    void operator()(const char *fieldName, std::nullptr_t);

    void operator()(const char *fieldName, bool v);

    void operator()(const char *fieldName, int v) {
        (*this)(fieldName, static_cast<int64_t>(v));
    }

    void operator()(const char *fieldName, int64_t v);

    void operator()(const char *fieldName, double v);

    void operator()(const char *fieldName, const char *v) {
        (*this)(fieldName, std::string(v));
    }

    void operator()(const char *fieldName, const std::string &v);

    void operator()(const char *fieldName, const JSON &v);

    template <typename T, enable_when<is_array_v<T>> = nullptr>
    void operator()(const char *fieldName, T &v) {
        auto s = JSONSerializer::asArray();
        for(auto &e : v) {
            s(nullptr, e);
        }
        this->append(fieldName, std::move(s).take());
    }

    template <typename T, enable_when<is_object_v<T>> = nullptr>
    void operator()(const char *fieldName, T &v) {
        auto s = JSONSerializer::asObject();
        jsonify(s, v);
        this->append(fieldName, std::move(s).take());
    }

    template <typename ...R>
    void operator()(const char *fieldName, Union<R...> &v) {
        ToJSON<sizeof...(R) - 1, R...>()(*this, fieldName, v);
    }

    template <typename T>
    void operator()(T &&v) {
        (*this)(nullptr, std::forward<T>(v));
    }

private:
    template <int N, typename ...R>
    struct ToJSON {
        void operator()(JSONSerializer &serializer, const char *fieldName, Union<R...> &value) const {
            if constexpr(N == -1) {
                serializer.append(fieldName, JSON());
            } else if(value.tag() == N) {
                using T = typename TypeByIndex<N, R...>::type;
                serializer(fieldName, ydsh::get<T>(value));
            } else {
                ToJSON<N - 1, R...>()(serializer, fieldName, value);
            }
        }
    };

    void append(const char *fieldName, JSON &&json);
};

class JSONValidator : public RefCount<JSONValidator> {
private:
    std::vector<std::string> errors;

public:
    const std::vector<std::string> &getErrors() const {
        return this->errors;
    }

    bool hasError() const {
        return !this->errors.empty();
    }

    std::string formatError() const;

    template <typename T, enable_when<JSON::TAG<T> != -1> = nullptr>
    bool validate(const JSON &value) {
        return this->validate(value, JSON::TAG<T>);
    }

    template <typename T, enable_when<JSON::TAG<T> != -1> = nullptr>
    bool validate(const JSON &value, const char *fieldName) {
        return this->validate(value, fieldName, JSON::TAG<T>);
    }

private:
    void appendError(const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));

    bool validate(const JSON &value, int required, const char *messagePrefix = "");

    bool validate(const JSON &value, const char *fieldName, int required);
};

class JSONDeserializer {
private:
    JSON root;
    IntrusivePtr<JSONValidator> validator;

public:
    explicit JSONDeserializer(JSON &&json) :
            root(std::move(json)),
            validator(IntrusivePtr<JSONValidator>::create()) {}

    JSONDeserializer(JSON &&json, IntrusivePtr<JSONValidator> validator) :
            root(std::move(json)), validator(std::move(validator)) {}

    const IntrusivePtr<JSONValidator> &getValidator() const {
        return this->validator;
    }

    bool hasError() const {
        return this->validator->hasError();
    }

    void operator()(const char *fieldName, std::nullptr_t &v);

    void operator()(const char *fieldName, bool &v);

    void operator()(const char *fieldName, int &v) {
        int64_t v1;
        (*this)(fieldName, v1);
        v = v1;
    }

    void operator()(const char *fieldName, int64_t &v);

    void operator()(const char *fieldName, double &v);

    void operator()(const char *fieldName, std::string &v);

    void operator()(const char *fieldName, JSON &v);

    template <typename T, enable_when<is_array_v<T>> = nullptr>
    void operator()(const char *fieldName, T &v) {
        JSON json = this->validateAndTakeArray(fieldName);
        if(json.isInvalid()) {
            return;
        }
        unsigned int size = json.asArray().size();
        for(unsigned int i = 0; i < size; i++) {
            JSONDeserializer deserializer(std::move(json.asArray()[i]), this->validator);
            array_element_t<T> element;
            deserializer(element);
            if(deserializer.hasError()) {
                break;
            }
            v.push_back(std::move(element));
        }
    }

    template <typename T, enable_when<is_object_v<T>> = nullptr>
    void operator()(const char *fieldName, T &v) {
        JSON json = this->validateAndTakeObject(fieldName);
        if(json.isInvalid()) {
            return;
        }
        JSONDeserializer deserializer(std::move(json));
        jsonify(deserializer, v);
    }

//    template <typename ...R>
//    void operator()(const char *fieldName, Union<R...> &v) {
//        FromJSON<sizeof...(R) - 1, R...>()(*this, fieldName, v);
//    }

    template <typename T>
    void operator()(T &&v) {
        (*this)(nullptr, std::forward<T>(v));
    }

private:
//    static bool isType(const JSON &json, TypeHolder<int>) {
//        return json.isLong();
//    }
//
//    template <typename T>
//    static bool isType(const JSON &json, TypeHolder<T>) {
//        return json.tag() == JSON::TAG<T>;
//    }
//
//    template <int N, typename ...R>
//    struct FromJSON {
//        void operator()(JSONDeserializer &deserializer, const char *fieldName, Union<R...> &value) const {
//            if constexpr(N > -1) {
//                using T = typename TypeByIndex<N, R...>::type;
//                if(isType(deserializer.root, TypeHolder<T>())) {
//                    T v;
//                    deserializer(fieldName, v);
//                    value = std::move(v);
//                } else {
//                    FromJSON<N - 1, R...>()(deserializer, fieldName, value);
//                }
//            }
//        }
//    };

    JSON validateAndTakeArray(const char *fieldName);

    JSON validateAndTakeObject(const char *fieldName);
};


} // namespace json
} // namespace ydsh

#endif //YDSH_TOOLS_SERIALIZE_H
