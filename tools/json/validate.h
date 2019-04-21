/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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
#include <misc/detect.hpp>
#include "json.h"

namespace ydsh {
namespace json {

class InterfaceMap;

class Validator {
private:
    const InterfaceMap &map;
    std::vector<std::string> errors;

public:
    explicit Validator(const InterfaceMap &map) : map(map) {}

    bool operator()(const std::string &ifaceName, const JSON &value);

    std::string formatError() const;

    void clearError() {
        this->errors.clear();
    }

    template <typename ...T>
    void appendError(T && ...v) {
        this->errors.emplace_back(std::forward<T>(v)...);
    }
};

class PrimitiveMatcher {
protected:
    const char *name;
    int tag;

public:
    constexpr PrimitiveMatcher() noexcept : name(""), tag(-1) {}
    constexpr PrimitiveMatcher(const char *name, int tag) noexcept : name(name), tag(tag) {}

    bool operator()(Validator &, const JSON &value) const {
        return this->tag == value.tag();
    }

    std::string str() const {
        return this->name;
    }
};

struct AnyMatcher {
    bool operator()(Validator &, const JSON &) const {
        return true;
    }

    std::string str() const {
        return "any";
    }
};

namespace __detail_matcher {

/**
 * workaround for gcc-5
 */
constexpr auto JSON_ARRAY_TAG = JSON::TAG<Array>;

} // namespace __detail_matcher

template <typename M>
class ArrayMatcher : public PrimitiveMatcher {
private:
    M matcher;

public:
    explicit constexpr ArrayMatcher(M matcher) noexcept :
            PrimitiveMatcher("Array", __detail_matcher::JSON_ARRAY_TAG), matcher(matcher) {}

    bool operator()(Validator &validator, const JSON &value) const {
        if(this->tag != value.tag()) {
            return false;
        }
        for(auto &e : value.asArray()) {
            if(!this->matcher(validator, e)) {
                return false;
            }
        }
        return true;
    }

    std::string str() const {
        std::string str = this->name;
        str += "<";
        str += this->matcher.str();
        str += ">";
        return str;
    }
};

struct ObjectMatcher : public PrimitiveMatcher {
    /**
     *
     * @param name
     * if empty string, match all of objects
     */
    explicit constexpr ObjectMatcher(const char *name) noexcept :
                PrimitiveMatcher(name, JSON::TAG<Object>) {}

    bool operator()(Validator &validator, const JSON &value) const {
        return validator(this->name, value);
    }

    std::string str() const {
        return this->name;
    }
};

template <typename L, typename R>
class UnionMatcher : public PrimitiveMatcher {
private:
    L left;
    R right;

public:
    constexpr UnionMatcher(L left, R right) noexcept :
            PrimitiveMatcher(), left(left), right(right) {}

    constexpr UnionMatcher(const char *alias, L left, R right) noexcept :
            PrimitiveMatcher(alias, -1), left(left), right(right) {}

    bool operator()(Validator &validator, const JSON &value) const {
        if(this->left(validator, value)) {
            return true;
        }
        validator.clearError();
        return this->right(validator, value);
    }

    std::string str() const {
        if(this->name[0] != '\0') {
            return this->name;
        }

        std::string str = this->left.str();
        str += " | ";
        str += this->right.str();
        return str;
    }
};

template <typename T>
class OptMatcher {
private:
    T matcher;

public:
    constexpr explicit OptMatcher(T matcher) : matcher(matcher) {}

    bool operator()(Validator &validator, const JSON &value) const {
        if(value.isInvalid()) {
            return true;    // always true.
        }
        return this->matcher(validator, value);
    }

    std::string str() const {
        std::string str = "Option<";
        str += this->matcher.str();
        str += ">";
        return str;
    }
};

template <typename T>
struct isOptMatcher : std::false_type {};

template <typename T>
struct isOptMatcher<OptMatcher<T>> : std::true_type {};

struct Matcher {
    virtual bool operator()(Validator &validator, const JSON &value) const = 0;
    virtual std::string str() const = 0;
    virtual bool required() const = 0;
    virtual ~Matcher() = default;
};

namespace __detail_matcher_detector {

template <typename T>
using has_apply = decltype(&T::operator());

template <typename T>
using has_str = decltype(&T::str);

template <typename T>
using has_apply_member = decltype(std::is_same<detected_t<has_apply, T>,
        decltype(&Matcher::operator())>::value);

template <typename T>
using has_str_member = decltype(std::is_same<detected_t<has_str, T>,
        decltype(&Matcher::str)>::value);

} // namespace __detail_matcher_detector

template <typename T>
constexpr auto has_matcher_iface_v =
        is_detected_v<__detail_matcher_detector::has_apply_member, T>
                && is_detected_v<__detail_matcher_detector::has_str_member, T>;

// helper function for type matcher construction

constexpr auto object(const char *name) {
    return ObjectMatcher(name);
}

template <typename T, enable_when<has_matcher_iface_v<T>> = nullptr>
constexpr auto array(T matcher) {
    return ArrayMatcher<T>(matcher);
}

template <typename L, typename R, enable_when<has_matcher_iface_v<L> && has_matcher_iface_v<R>> = nullptr>
constexpr auto operator|(L left, R right) {
    return UnionMatcher<L, R>(left, right);
}

template <typename T, enable_when<has_matcher_iface_v<T>> = nullptr>
constexpr auto operator!(T matcher) {
    return OptMatcher<T>(matcher);
}

constexpr auto integer = PrimitiveMatcher("integer", JSON::TAG<long>);
constexpr auto number = UnionMatcher<PrimitiveMatcher, PrimitiveMatcher>(
        "number",
        PrimitiveMatcher("long", JSON::TAG<long>),
        PrimitiveMatcher("double", JSON::TAG<double>)
);

constexpr auto string = PrimitiveMatcher("string", JSON::TAG<String>);
constexpr auto boolean = PrimitiveMatcher("boolean", JSON::TAG<bool>);
constexpr auto null = PrimitiveMatcher("null", JSON::TAG<std::nullptr_t>);
constexpr auto any = AnyMatcher();

template <typename T>
struct TypeMatcherConstructor {};

template <>
struct TypeMatcherConstructor<int> {
    static constexpr auto value = integer;
};

template <>
struct TypeMatcherConstructor<unsigned int> : TypeMatcherConstructor<int> {};

template <>
struct TypeMatcherConstructor<String> {
    static constexpr auto value = string;
};

template <>
struct TypeMatcherConstructor<bool> {
    static constexpr auto value = boolean;
};

template <>
struct TypeMatcherConstructor<std::nullptr_t> {
    static constexpr auto value = null;
};

template <>
struct TypeMatcherConstructor<JSON> {
    static constexpr auto value = any;
};

template <typename T>
struct TypeMatcherConstructor<std::vector<T>> {
    static constexpr auto value = array(TypeMatcherConstructor<T>::value);
};

template <typename T>
struct TypeMatcherConstructor<OptionalBase<T>> {
    static constexpr auto value = !(TypeMatcherConstructor<T>::value);
};

template <typename T1>
constexpr auto createUnionMatcher(T1 t1) {
    return std::forward<T1>(t1);
}

template <typename T1, typename T2, typename ...Tn>
constexpr auto createUnionMatcher(T1 t1, T2 t2, Tn ...tn) {
    return t1 | createUnionMatcher(std::forward<T2>(t2), std::forward<Tn>(tn)...);
}

template <typename ...T>
struct TypeMatcherConstructor<Union<T...>> {
    static constexpr auto value = createUnionMatcher(TypeMatcherConstructor<T>::value...);
};

template <typename T>
constexpr auto toTypeMatcher = TypeMatcherConstructor<T>::value;

class Field {
private:
    template <typename T>
    struct MatcherHolder : public Matcher {
        T instance;

        explicit MatcherHolder(const T &m) : instance(std::move(m)) {}

        bool operator()(Validator &validator, const JSON &value) const override {
            return this->instance(validator, value);
        }

        std::string str() const override {
            return this->instance.str();
        }

        bool required() const override {
            return !isOptMatcher<T>::value;
        }
    };

    std::unique_ptr<Matcher> matcher;

public:
    explicit Field() = default;

    template <typename T, enable_when<has_matcher_iface_v<T>> = nullptr>
    explicit Field(const T &type) : matcher(new MatcherHolder<T>(type)) {}

    const Matcher &getMatcher() const {
        return *this->matcher;
    }

    bool isRequire() const {
        return this->matcher->required();
    }
};

struct Fields {
    using Entry = std::unordered_map<std::string, Field>;
    Entry value;

    Fields(std::initializer_list<std::pair<std::string, Field>> list);
};

template <typename T>
inline std::pair<std::string, Field> field(const char *name, T value) {
    return {name, Field(value)};
}

struct InterfaceBase {
    virtual ~InterfaceBase() = default;
    virtual const char *getName() const = 0;
    virtual bool match(Validator &validator, const JSON &json) const = 0;
};

class Interface : public InterfaceBase {
private:
    friend class Validator;
    using Entry = Fields::Entry;
    std::string name;
    Entry fields;

public:
    Interface() = default;

    Interface(const char *name, Fields &&fields) : name(name), fields(std::move(fields.value)) {}

    const char *getName() const override {
        return this->name.c_str();
    }

    const Entry &getFields() const {
        return this->fields;
    }

    bool match(Validator &validator, const JSON &json) const override;
};

struct VoidInterface : public InterfaceBase {
    friend class Validator;

    const char *getName() const override {
        return "void";
    }

    bool match(Validator &validator, const JSON &json) const override;
};

using InterfaceBasePtr = std::shared_ptr<InterfaceBase>;
using InterfacePtr = std::shared_ptr<Interface>;
using VoidInterfacePtr = std::shared_ptr<VoidInterface>;

class InterfaceMap {
private:
    CStringHashMap<InterfaceBasePtr> map;

public:
    InterfacePtr interface(const char *name, Fields &&fields);

    VoidInterfacePtr interface();

    const InterfaceBase *lookup(const std::string &name) const {
        return this->lookup(name.c_str());
    }

    const InterfaceBase *lookup(const char *name) const;

private:
    InterfaceBasePtr add(InterfaceBasePtr &&iface);
};

} // namespace json
} // namespace ydsh

#endif //YDSH_TOOLS_VALIDATE_H
