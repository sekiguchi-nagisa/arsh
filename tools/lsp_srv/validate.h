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

#ifndef TOOLS_VALIDATE_H
#define TOOLS_VALIDATE_H

#include <unordered_map>

#include "json.h"

namespace json {

class Validator;

/**
 * for json type validation
 */
struct Matcher {
    virtual bool match(Validator &validator, const JSON &value) const = 0;

    virtual std::string str() const = 0;

    virtual ~Matcher() = default;
};

class TypeMatcher : public Matcher {
private:
    friend class Validator;

    const std::string name;
    int tag;

public:
    TypeMatcher() : tag(-1) {}

    TypeMatcher(const char *name, int tag) : name(name), tag(tag) {}

    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

struct AnyMatcher : public TypeMatcher {
    friend class Validator;

    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

class ObjectMatcher : public TypeMatcher {
private:
    friend class Validator;

    std::string name;

public:
    explicit ObjectMatcher(const char *name) : name(name) {}

    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

using TypeMatcherPtr = std::shared_ptr<TypeMatcher>;

class UnionMatcher : public Matcher {
private:
    friend class Validator;

    TypeMatcherPtr left;
    TypeMatcherPtr right;

public:
    UnionMatcher(TypeMatcherPtr left, TypeMatcherPtr right) : left(left), right(right) {}

    bool match(Validator &validator, const JSON &value) const override;
    std::string str() const override;
};

using MatcherPtr = std::shared_ptr<Matcher>;

class Field {
private:
    MatcherPtr matcher;
    bool opt;

public:
    Field(MatcherPtr type, bool opt = false) : matcher(type), opt(opt) {}

    const Matcher &getMatcher() const {
        return *this->matcher;
    }

    bool optional() const {
        return this->opt;
    }
};

class Interface {
private:
    using Entry = std::unordered_map<std::string, Field>;
    Entry fields;

public:
    Interface &field(const char *name, MatcherPtr type) {
        return this->addField(name, type, false);
    }

    Interface &optField(const char *name, MatcherPtr type) {
        return this->addField(name, type, true);
    }

    const Entry &getFields() const {
        return this->fields;
    }

    bool match(Validator &validator, const JSON &value) const;

private:
    Interface &addField(const char *name, MatcherPtr type, bool opt);
};

class InterfaceMap {
private:
    std::unordered_map<std::string, Interface> map;

public:
    Interface &interface(const char *name);

    const Interface *lookup(const std::string &name) const;
};

class Validator {
private:
    const InterfaceMap &map;
    std::vector<std::string> errors;

public:
    explicit Validator(const InterfaceMap &map) : map(map) {}

    bool match(const TypeMatcher &matcher, const JSON &value);
    bool match(const AnyMatcher &matcher, const JSON &value);
    bool match(const ObjectMatcher &matcher, const JSON &value);
    bool match(const UnionMatcher &matcher, const JSON &value);
    bool match(const std::string &ifaceName, const JSON &value);
};

} // namespace json

#endif //TOOLS_VALIDATE_H
