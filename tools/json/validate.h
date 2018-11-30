/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_VALIDATE_H
#define YDSH_TOOLS_VALIDATE_H

#include <unordered_map>

#include <misc/hash.hpp>
#include "json.h"

namespace json {

class Validator;

/**
 * for json type validation
 */
class TypeMatcher {
protected:
    friend class Validator;

    const std::string name;
    const int tag;

public:
    TypeMatcher() : tag(-1) {}

    TypeMatcher(const char *name, int tag) : name(name), tag(tag) {}

    virtual ~TypeMatcher() = default;

    virtual bool match(Validator &validator, const JSON &value) const;
    virtual std::string str() const;
};

using TypeMatcherPtr = std::shared_ptr<TypeMatcher>;

struct AnyMatcher : public TypeMatcher {
    friend class Validator;

    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

class ArrayMatcher : public TypeMatcher {
private:
    friend class Validator;

    const TypeMatcherPtr matcher;

public:
    explicit ArrayMatcher(TypeMatcherPtr matcher) :
            TypeMatcher("Array", JSON::Tag<Array>::value), matcher(std::move(matcher)) {}
    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

struct ObjectMatcher : public TypeMatcher {
    friend class Validator;

    /**
     *
     * @param name
     * if empty string, match all of objects
     */
    explicit ObjectMatcher(const char *name) : TypeMatcher(name, JSON::Tag<Object>::value) {}

    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

class UnionMatcher : public TypeMatcher {
private:
    friend class Validator;

    TypeMatcherPtr left;
    TypeMatcherPtr right;

public:
    UnionMatcher(TypeMatcherPtr left, TypeMatcherPtr right) :
            left(std::move(left)), right(std::move(right)) {}

    UnionMatcher(const char *alias, TypeMatcherPtr left, TypeMatcherPtr right) :
            TypeMatcher(alias, -1), left(std::move(left)), right(std::move(right)) {}

    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

// helper method and constant for interface(schema) definition
extern const TypeMatcherPtr number;
extern const TypeMatcherPtr string;
extern const TypeMatcherPtr boolean;
extern const TypeMatcherPtr null;
extern const TypeMatcherPtr any;

inline TypeMatcherPtr object(const char *name) {
    return std::make_shared<ObjectMatcher>(name);
}

inline TypeMatcherPtr array(TypeMatcherPtr e) {
    return std::make_shared<ArrayMatcher>(e);
}

inline TypeMatcherPtr operator|(TypeMatcherPtr left, TypeMatcherPtr right) {
    return std::make_shared<UnionMatcher>(left, right);
}

class Field {
private:
    TypeMatcherPtr matcher;
    bool require;

public:
    explicit Field() : matcher(), require(true) {}

    Field(TypeMatcherPtr type, bool require) : matcher(std::move(type)), require(require) {}

    explicit Field(TypeMatcherPtr type) : Field(std::move(type), true) {}

    Field(Field &&v) noexcept : matcher(std::move(v.matcher)), require(v.require) {}

    Field &operator=(Field &&v) noexcept {
        auto tmp = std::move(v);
        std::swap(this->matcher, tmp.matcher);
        std::swap(this->require, tmp.require);
        return *this;
    }

    const TypeMatcher &getMatcher() const {
        return *this->matcher;
    }

    bool isRequire() const {
        return this->require;
    }
};

struct Fields {
    using Entry = std::unordered_map<std::string, Field>;
    Entry value;

    Fields(std::initializer_list<std::pair<std::string, Field>> list);
};

template <typename ...Arg>
inline std::pair<std::string, Field> field(const char *name, Arg&& ...arg) {
    return {name, Field(std::forward<Arg>(arg)...)};
}

class Interface {
private:
    using Entry = Fields::Entry;
    std::string name;
    Entry fields;

public:
    Interface() = default;

    Interface(const char *name, Fields &&fields) : name(name), fields(std::move(fields.value)) {}

    const std::string &getName() const {
        return this->name;
    }

    const Entry &getFields() const {
        return this->fields;
    }
};

using InterfacePtr = std::shared_ptr<Interface>;

class InterfaceMap {
private:
    ydsh::CStringHashMap<InterfacePtr> map;

public:
    const InterfacePtr &interface(const char *name, Fields &&fields);

    const Interface *lookup(const std::string &name) const {
        return this->lookup(name.c_str());
    }

    const Interface *lookup(const char *name) const;
};

class Validator {
private:
    const InterfaceMap &map;
    std::vector<std::string> errors;

public:
    explicit Validator(const InterfaceMap &map) : map(map) {}

    bool match(const TypeMatcher &matcher, const JSON &value);
    bool match(const AnyMatcher &matcher, const JSON &value);
    bool match(const ArrayMatcher &matcher, const JSON &value);
    bool match(const ObjectMatcher &matcher, const JSON &value);
    bool match(const UnionMatcher &matcher, const JSON &value);
    bool match(const std::string &ifaceName, const JSON &value);

    bool operator()(const std::string &ifaceName, const JSON &value) {
        this->errors.clear();
        return this->match(ifaceName, value);
    }

    std::string formatError() const;
};

} // namespace json

#endif //YDSH_TOOLS_VALIDATE_H
