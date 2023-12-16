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

#ifndef YDSH_TOOLS_JSON_SERIALIZE_H
#define YDSH_TOOLS_JSON_SERIALIZE_H

#include "json.h"

namespace arsh::json {

template <typename T>
struct is_array : std::false_type {};

template <typename T>
struct is_array<std::vector<T>> : std::true_type {};

template <typename T>
constexpr bool is_array_v = is_array<T>::value;

template <typename T>
struct is_map : std::false_type {};

template <typename T>
struct is_map<std::map<std::string, T>> : std::true_type {};

template <typename T>
constexpr bool is_map_v = is_map<T>::value;

template <typename T>
constexpr bool is_string_v = std::is_same_v<T, String>;

template <typename T>
struct is_union : std::false_type {};

template <typename... R>
struct is_union<Union<R...>> : std::true_type {};

template <typename T>
constexpr bool is_union_v = is_union<T>::value;

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<OptionalBase<T>> : std::true_type {};

template <typename T>
constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
constexpr bool is_object_v = !is_string_v<T> && !is_array_v<T> && !is_map_v<T> && !is_union_v<T> &&
                             !is_optional_v<T> && !std::is_same_v<T, JSON> && std::is_class_v<T>;

template <typename T>
struct array_element {};

template <typename T>
struct array_element<std::vector<T>> {
  using type = T;
};

template <typename T>
using array_element_t = typename array_element<T>::type;

template <typename T>
struct map_value {};

template <typename T>
struct map_value<std::map<std::string, T>> {
  using type = T;
};

template <typename T>
using map_value_t = typename map_value<T>::type;

class JSONSerializer {
private:
  /**
   * final serialized value
   */
  JSON result;

  static JSONSerializer asArray();

  static JSONSerializer asObject();

public:
  const JSON &get() const { return this->result; }

  JSON take() && { return std::move(result); }

  void operator()(const char *fieldName, std::nullptr_t);

  void operator()(const char *fieldName, bool v);

  template <typename T, enable_when<(std::is_signed_v<T> || std::is_unsigned_v<T>)&&sizeof(T) <=
                                    sizeof(int64_t)> = nullptr>
  void operator()(const char *fieldName, T v) {
    (*this)(fieldName, static_cast<int64_t>(v));
  }

  void operator()(const char *fieldName, int64_t v);

  void operator()(const char *fieldName, double v);

  void operator()(const char *fieldName, const char *v) { (*this)(fieldName, std::string(v)); }

  void operator()(const char *fieldName, const std::string &v);

  void operator()(const char *fieldName, const JSON &v);

  template <typename T, enable_when<is_array_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    auto s = JSONSerializer::asArray();
    for (auto &e : v) {
      s(nullptr, e);
    }
    this->append(fieldName, std::move(s).take());
  }

  template <typename T, enable_when<is_map_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    auto s = JSONSerializer::asObject();
    for (auto &e : v) {
      s(e.first.c_str(), e.second);
    }
    this->append(fieldName, std::move(s).take());
  }

  template <typename T, enable_when<is_object_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    auto s = JSONSerializer::asObject();
    jsonify(s, v);
    this->append(fieldName, std::move(s).take());
  }

  template <typename T, enable_when<std::is_enum_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    JSONSerializer s;
    jsonify(s, v);
    this->append(fieldName, std::move(s).take());
  }

  template <typename... R>
  void operator()(const char *fieldName, Union<R...> &v) {
    ToJSON<sizeof...(R) - 1, R...>()(*this, fieldName, v);
  }

  template <typename T, enable_when<is_optional_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    if (v.hasValue()) {
      (*this)(fieldName, v.unwrap());
    }
  }

  template <typename T>
  void operator()(T &&v) {
    (*this)(nullptr, std::forward<T>(v));
  }

private:
  template <int N, typename... R>
  struct ToJSON {
    void operator()(JSONSerializer &serializer, const char *fieldName, Union<R...> &value) const {
      if constexpr (N == -1) {
        serializer.append(fieldName, JSON());
      } else if (value.tag() == N) {
        using T = typename TypeByIndex<N, R...>::type;
        serializer(fieldName, arsh::get<T>(value));
      } else {
        ToJSON<N - 1, R...>()(serializer, fieldName, value);
      }
    }
  };

  void append(const char *fieldName, JSON &&json);
};

class ValidationError {
private:
  std::vector<std::string> messages;

public:
  bool hasError() const { return !this->messages.empty(); }

  std::string formatError() const;

  void appendError(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

class JSONDeserializerImpl {
private:
  JSON &value;
  ValidationError &validationError;
  const bool validOnly;

public:
  JSONDeserializerImpl(JSON &value, ValidationError &error, bool validOnly = false)
      : value(value), validationError(error), validOnly(validOnly) {}

  bool hasError() const { return this->validationError.hasError(); }

  void operator()(const char *fieldName, std::nullptr_t &) {
    this->validateField<std::nullptr_t>(fieldName);
  }

  void operator()(const char *fieldName, bool &v);

  template <typename T, enable_when<(std::is_signed_v<T> || std::is_unsigned_v<T>)&&sizeof(T) <=
                                    sizeof(int64_t)> = nullptr>
  void operator()(const char *fieldName, T &v) {
    int64_t v1;
    (*this)(fieldName, v1);
    if (!this->hasError()) {
      v = static_cast<T>(v1);
    }
  }

  void operator()(const char *fieldName, int64_t &v);

  void operator()(const char *fieldName, double &v);

  void operator()(const char *fieldName, std::string &v);

  void operator()(const char *fieldName, JSON &v);

  template <typename T, enable_when<is_array_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    JSON *json = this->validateField<Array>(fieldName);
    if (!json || this->validOnly) {
      return;
    }
    for (auto &e : json->asArray()) {
      JSONDeserializerImpl deserializer(e, this->validationError, this->validOnly);
      array_element_t<T> element;
      deserializer(element);
      if (deserializer.hasError()) {
        break;
      }
      if (!deserializer.validOnly) {
        v.push_back(std::move(element));
      }
    }
  }

  template <typename T, enable_when<is_map_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    JSON *json = this->validateField<Object>(fieldName);
    if (!json || (this->validOnly && fieldName)) {
      return;
    }
    for (auto &e : json->asObject()) {
      JSONDeserializerImpl deserializer(e.second, this->validationError, this->validOnly);
      map_value_t<T> element;
      deserializer(element);
      if (deserializer.hasError()) {
        break;
      }
      if (!deserializer.validOnly) {
        v.insert(std::make_pair(std::move(e.first), std::move(element)));
      }
    }
  }

  template <typename T, enable_when<is_object_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    JSON *json = this->validateField<Object>(fieldName);
    if (!json || (this->validOnly && fieldName)) {
      return;
    }
    JSONDeserializerImpl deserializer(*json, this->validationError, this->validOnly);
    jsonify(deserializer, v);
  }

  template <typename T, enable_when<std::is_enum_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    JSON *json = this->validateField(fieldName, -1);
    if (!json || (this->validOnly && fieldName)) {
      return;
    }
    JSONDeserializerImpl deserializer(*json, this->validationError, this->validOnly);
    jsonify(deserializer, v);
  }

  template <typename... R>
  void operator()(const char *fieldName, Union<R...> &v) {
    JSON *json = this->validateField(fieldName, -1);
    if (!json) {
      return;
    }
    ValidationError e;
    FromJSON<0, R...> fromJSON(e);
    JSONDeserializerImpl deserializer(*json, this->validationError, this->validOnly);
    fromJSON(deserializer, v);
  }

  template <typename T, enable_when<is_optional_v<T>> = nullptr>
  void operator()(const char *fieldName, T &v) {
    JSON *json = this->validateField(fieldName, -1, true);
    if (!json || json->isInvalid()) {
      return;
    }
    using base_type = typename T::base_type;
    JSONDeserializerImpl deserializer(*json, this->validationError, this->validOnly);
    deserializer(static_cast<base_type &>(v));
  }

  template <typename T>
  void operator()(T &&v) {
    (*this)(nullptr, std::forward<T>(v));
  }

private:
  template <int N, typename... R>
  struct FromJSON {
    ValidationError &error;

    explicit FromJSON(ValidationError &error) : error(error) {}

    bool validate(JSON &json) {
      if constexpr (N > 0) {
        this->error = ValidationError();
      }
      JSONDeserializerImpl deserializer(json, this->error, true);
      using T = typename TypeByIndex<N, R...>::type;
      T t;
      deserializer(t);
      return !deserializer.hasError();
    }

    void operator()(JSONDeserializerImpl &deserializer, Union<R...> &ret) {
      if constexpr (N == sizeof...(R)) {
        (void)ret;
        deserializer.validationError = std::move(this->error);
      } else if (this->validate(deserializer.value)) {
        if (!deserializer.validOnly) {
          using T = typename TypeByIndex<N, R...>::type;
          T v;
          deserializer(v);
          ret = std::move(v);
        }
      } else {
        FromJSON<N + 1, R...>(this->error)(deserializer, ret);
      }
    }
  };

  template <typename T, enable_when<JSON::TAG<T> != -1> = nullptr>
  JSON *validateField(const char *fieldName) {
    return this->validateField(fieldName, JSON::TAG<T>);
  }

  /**
   * find and check field type
   * @param fieldName
   * if fieldName null, return this->value
   * @param tag
   * @param op
   * @return
   * resolved field
   */
  JSON *validateField(const char *fieldName, int tag, bool optional = false);
};

class JSONDeserializer {
private:
  JSON root;
  ValidationError validationError;

public:
  explicit JSONDeserializer(JSON &&json) : root(std::move(json)) {}

  const ValidationError &getValidationError() const { return this->validationError; }

  bool hasError() const { return this->validationError.hasError(); }

  template <typename T>
  void operator()(T &&v) {
    JSONDeserializerImpl deserializer(this->root, this->validationError);
    deserializer(std::forward<T>(v));
  }
};

template <typename T>
struct is_serialize : std::false_type {};

template <>
struct is_serialize<JSONSerializer> : std::true_type {};

template <typename T>
constexpr bool is_serialize_v = is_serialize<T>::value;

template <typename T>
struct is_deserialize : std::false_type {};

template <>
struct is_deserialize<JSONDeserializerImpl> : std::true_type {};

template <typename T>
constexpr bool is_deserialize_v = is_deserialize<T>::value;

template <typename S, typename T>
void jsonify(S &s, T &v) {
  if constexpr (std::is_enum_v<T>) {
    if constexpr (is_serialize_v<S>) {
      s(static_cast<std::underlying_type_t<T>>(v));
    } else if constexpr (is_deserialize_v<S>) {
      std::underlying_type_t<T> v1;
      s(v1);
      if (!s.hasError()) {
        v = static_cast<T>(v1);
      }
    } else {
      static_assert(!is_deserialize_v<S>);
    }
  } else {
    v.jsonify(s);
  }
}

} // namespace arsh::json

#endif // YDSH_TOOLS_JSON_SERIALIZE_H
