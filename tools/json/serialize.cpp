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

#include <cassert>
#include <cstdarg>

#include "serialize.h"

namespace ydsh::json {

// ############################
// ##     JSONSerializer     ##
// ############################

JSONSerializer JSONSerializer::asArray() {
  Array v;
  JSONSerializer s;
  s.result = std::move(v);
  return s;
}

JSONSerializer JSONSerializer::asObject() {
  Object v;
  JSONSerializer s;
  s.result = std::move(v);
  return s;
}

void JSONSerializer::operator()(const char *fieldName, std::nullptr_t) {
  this->append(fieldName, JSON(nullptr));
}

void JSONSerializer::operator()(const char *fieldName, bool v) { this->append(fieldName, v); }

void JSONSerializer::operator()(const char *fieldName, int64_t v) { this->append(fieldName, v); }

void JSONSerializer::operator()(const char *fieldName, double v) { this->append(fieldName, v); }

void JSONSerializer::operator()(const char *fieldName, const std::string &v) {
  this->append(fieldName, v);
}

void JSONSerializer::operator()(const char *fieldName, const JSON &v) {
  this->append(fieldName, JSON(v));
}

void JSONSerializer::append(const char *fieldName, JSON &&json) {
  if (this->result.isArray()) {
    this->result.asArray().push_back(std::move(json));
  } else if (this->result.isObject()) {
    assert(fieldName);
    this->result.asObject().emplace(fieldName, std::move(json));
  } else if (this->result.isInvalid()) {
    this->result = std::move(json);
  } else {
    fatal("broken");
  }
}

// ###########################
// ##     JSONValidator     ##
// ###########################

std::string ValidationError::formatError() const {
  std::string str;
  if (!this->messages.empty()) {
    str = this->messages.back();
    for (auto iter = this->messages.rbegin() + 1; iter != this->messages.rend(); ++iter) {
      str += "\n    from: ";
      str += *iter;
    }
  }
  return str;
}

void ValidationError::appendError(const char *fmt, ...) {
  va_list arg;
  va_start(arg, fmt);
  char *str = nullptr;
  if (vasprintf(&str, fmt, arg) == -1) {
    fatal_perror("");
  }
  va_end(arg);

  this->messages.emplace_back(str);
  free(str);
}

// ##################################
// ##     JSONDeserializerImpl     ##
// ##################################

void JSONDeserializerImpl::operator()(const char *fieldName, bool &v) {
  if (JSON * json; (json = this->validateField<bool>(fieldName))) {
    v = json->asBool();
  }
}

void JSONDeserializerImpl::operator()(const char *fieldName, int64_t &v) {
  if (JSON * json; (json = this->validateField<int64_t>(fieldName))) {
    v = json->asLong();
  }
}

void JSONDeserializerImpl::operator()(const char *fieldName, double &v) {
  if (JSON * json; (json = this->validateField<double>(fieldName))) {
    v = json->asDouble();
  }
}

void JSONDeserializerImpl::operator()(const char *fieldName, std::string &v) {
  if (JSON * json; (json = this->validateField<String>(fieldName))) {
    if (!this->validOnly) {
      v = std::move(json->asString());
    }
  }
}

void JSONDeserializerImpl::operator()(const char *fieldName, JSON &v) {
  if (JSON * json; (json = this->validateField(fieldName, -1))) {
    if (!this->validOnly) {
      v = std::move(*json);
    }
  }
}

static const char *getJSONTypeStr(int tag) {
  switch (tag) {
  case JSON::TAG<std::nullptr_t>:
    return "null";
  case JSON::TAG<bool>:
    return "bool";
  case JSON::TAG<int64_t>:
    return "long";
  case JSON::TAG<double>:
    return "double";
  case JSON::TAG<String>:
    return "string";
  case JSON::TAG<Object>:
    return "object";
  case JSON::TAG<Array>:
    return "array";
  default:
    break;
  }
  return "invalid";
}

static const char *getJSONTypeStr(const JSON &v) { return getJSONTypeStr(v.tag()); }

JSON *JSONDeserializerImpl::validateField(const char *fieldName, int tag, bool optional) {
  if (this->hasError()) {
    return nullptr;
  }
  JSON *json;
  if (fieldName) {
    if (!is<Object>(this->value)) {
      this->validationError.appendError("require `%s', but is `%s'",
                                        getJSONTypeStr(JSON::TAG<Object>),
                                        getJSONTypeStr(this->value));
      return nullptr;
    }
    auto &obj = this->value.asObject();
    auto iter = obj.find(fieldName);
    if (iter == obj.end()) {
      if (!optional) {
        this->validationError.appendError("undefined field `%s'", fieldName);
      }
      return nullptr;
    }
    json = &iter->second;
  } else {
    json = &this->value;
  }

  if (tag == -1) {
    return json;
  }
  if (json->tag() != tag) {
    std::string prefix;
    if (fieldName) {
      prefix = "field ";
      prefix += "`";
      prefix += fieldName;
      prefix += "'";
    }
    this->validationError.appendError("%srequire `%s', but is `%s'", prefix.c_str(),
                                      getJSONTypeStr(tag), getJSONTypeStr(*json));
    return nullptr;
  }
  return json;
}

} // namespace ydsh::json