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

#ifndef YDSH_TOOLS_VALIDATE_HPP
#define YDSH_TOOLS_VALIDATE_HPP

#include <unordered_map>
#include <tuple>

#include <misc/hash.hpp>
#include <misc/detect.hpp>
#include "json.h"

namespace ydsh {
namespace json {

class Validator {
private:
    std::vector<std::string> errors;

public:
    std::string formatError() const {
        std::string str;
        if(!this->errors.empty()) {
            str = this->errors.back();
            for(auto iter = this->errors.rbegin() + 1; iter != this->errors.rend(); ++iter) {
                str += "\n    from: ";
                str += *iter;
            }
        }
        return str;
    }

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

template <typename M>
class ArrayMatcher {
private:
    M matcher;

public:
    explicit constexpr ArrayMatcher(M matcher) noexcept : matcher(matcher) {}

    bool operator()(Validator &validator, const JSON &value) const {
        if(!value.isArray()) {
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
        std::string str = "Array<";
        str += this->matcher.str();
        str += ">";
        return str;
    }
};

template <typename T>
class ObjectMatcher {
private:
    const T &ref;

public:
    constexpr explicit ObjectMatcher(const T &iface) noexcept : ref(iface) {}

    bool operator()(Validator &validator, const JSON &value) const {
        return this->ref(validator, value);
    }

    std::string str() const {
        return this->ref.str();
    }
};

template <typename L, typename R>
class UnionMatcher {
private:
    const char *alias;
    L left;
    R right;

public:
    constexpr UnionMatcher(L left, R right) noexcept : alias(""), left(left), right(right) {}

    constexpr UnionMatcher(const char *alias, L left, R right) noexcept : alias(alias), left(left), right(right) {}

    bool operator()(Validator &validator, const JSON &value) const {
        if(this->left(validator, value)) {
            return true;
        }
        validator.clearError();
        return this->right(validator, value);
    }

    std::string str() const {
        if(this->alias && *this->alias != '\0') {
            return this->alias;
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
    constexpr explicit OptMatcher(T matcher) noexcept : matcher(matcher) {}

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

template <typename T>
class FieldMatcher {
private:
    const char *name;
    T matcher;

public:
    constexpr FieldMatcher(const char *name, T matcher) noexcept : name(name), matcher(matcher) {}

    const char *getName() const {
        return this->name;
    }

    const T &getMatcher() const {
        return this->matcher;
    }

    constexpr bool required() const {
        return !isOptMatcher<T>::value;
    }
};

template <typename ...T>
class InterfaceMatcher {
private:
    const char *name;
    std::tuple<FieldMatcher<T>...> fields;

public:
    constexpr InterfaceMatcher(const char *name, std::tuple<FieldMatcher<T>...> fields) noexcept : name(name), fields(fields) {}

    bool operator()(Validator &validator, const JSON &value) const {
        return value.isObject() && this->match<0>(validator, value.asObject());
    }

    constexpr const char *getName() const {
        return this->name;
    }

    std::string str() const {
        return this->getName();
    }

private:
    template <std::size_t I, enable_when<I == sizeof...(T)> = nullptr>
    bool match(Validator &, const Object &) const {
        return true;
    }

    template <std::size_t I, enable_when<I < sizeof...(T)> = nullptr>
    bool match(Validator &validator, const Object &value) const {
        auto &field = std::get<I>(this->fields);
        const auto &iter = value.find(field.getName());
        bool hasField = iter != value.end();
        if(!hasField && field.required()) {
            std::string str = "require field `";
            str += field.getName();
            str += "' in `";
            str += this->getName();
            str += "'";
            validator.appendError(std::move(str));
            return false;
        }

        if(hasField) {
            auto &matcher = field.getMatcher();
            if(!matcher(validator, iter->second)) {
                std::string str = "field `";
                str += field.getName();
                str += "' requires `";
                str += matcher.str();
                str += "' type in `";
                str += this->getName();
                str += "', but actual: ";
                str += iter->second.serialize(2);
                validator.appendError(std::move(str));
                return false;
            }
        }
        return this->match<I + 1>(validator, value);
    }
};

template <>
class InterfaceMatcher<std::nullptr_t> {
public:
    bool operator()(Validator &, const JSON &value) const {
        return value.isObject();
    }

    constexpr const char *getName() const {
        return "object";
    }

    std::string str() const {
        return this->getName();
    }
};

struct Matcher {
    virtual bool operator()(Validator &validator, const JSON &value) const = 0;
    virtual std::string str() const = 0;
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

template <typename T, enable_when<has_matcher_iface_v<T>> = nullptr>
constexpr auto object(const T &iface) {
    return ObjectMatcher<T>(iface);
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
constexpr auto opt(T matcher) {
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

template <>
class InterfaceMatcher<void> {
public:
    bool operator()(Validator &validator, const JSON &value) const {
        constexpr auto matcher = opt(null);
        return matcher(validator, value);
    }

    constexpr const char *getName() const {
        return "void";
    }

    std::string str() const {
        return this->getName();
    }
};

template <typename T, enable_when<has_matcher_iface_v<T>> = nullptr>
constexpr FieldMatcher<T> field(const char *name, T m) {
    return FieldMatcher<T>(name, m);
}

template <typename ...T>
constexpr auto createInterface(const char *name, FieldMatcher<T>... fields) {
    return InterfaceMatcher<T...>(name, std::make_tuple(fields...));
}


constexpr auto anyIface = InterfaceMatcher<std::nullptr_t>();
constexpr auto voidIface = InterfaceMatcher<void>();
constexpr auto anyObj = object(anyIface);   //NOLINT

template <typename>
struct InterfaceConstructor {};

template <typename, typename = void_t<>>
struct TypeMatcherConstructor {};

template <typename T>
struct TypeMatcherConstructor<T, std::enable_if_t<sizeof(T) == sizeof(int)
        && std::is_integral<T>::value, void>> {
    static constexpr auto value = integer;
};

template <typename T>
struct TypeMatcherConstructor<T, std::enable_if_t<sizeof(T) == sizeof(int)
        && std::is_enum<T>::value, void>> {
    static constexpr auto value = integer;
};

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
    static constexpr auto value = opt(TypeMatcherConstructor<T>::value);
};

template <typename T>
struct TypeMatcherConstructor<T, void_t<decltype(InterfaceConstructor<T>::value)>> {
    static constexpr auto value = object(InterfaceConstructor<T>::value);
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

class InterfaceWrapper {
private:
    template <typename T>
    struct Holder : public Matcher {
        const T &iface;

        explicit Holder(const T &iface) noexcept : iface(iface) {}

        bool operator()(Validator &validator, const JSON &value) const override {
            if(!this->iface(validator, value)) {
                std::string str = "requires `";
                str += this->iface.str();
                str += "' type";
                validator.appendError(std::move(str));
                return false;
            }
            return true;
        }

        std::string str() const override {
            return this->iface.str();
        }
    };

    std::unique_ptr<Matcher> instance;

public:
    template <typename T>
    InterfaceWrapper(const T &ref) : instance(new Holder<T>(ref)) {}    //NOLINT

    template <typename ...Arg>
    auto operator()(Arg&& ...arg) const {
        return (*this->instance)(std::forward<Arg>(arg)...);
    }
};

} // namespace json
} // namespace ydsh

#define DEFINE_JSON_VALIDATE_FIELD(T, f) ,field(#f, toTypeMatcher<decltype(T::f)>)

#define DEFINE_JSON_VALIDATE_INTERFACE(iface) \
template <> struct InterfaceConstructor<iface> { \
    static constexpr auto value = createInterface(#iface \
        EACH_ ## iface ## _FIELD(iface, DEFINE_JSON_VALIDATE_FIELD)); \
    using type = decltype(value); \
}; \
constexpr InterfaceConstructor<iface>::type InterfaceConstructor<iface>::value

#endif //YDSH_TOOLS_VALIDATE_HPP
