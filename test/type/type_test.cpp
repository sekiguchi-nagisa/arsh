#include "gtest/gtest.h"

#include <type.h>
#include <type_checker.h>
#include <symbol_table.h>

using namespace ydsh;


struct Type {};

template <const char *&value>
struct BaseType : Type {};

#define DEFINE_TYPE(name) \
namespace __detail_type {  \
    const char * name ## _v = #name; \
} \
using name ## _t = BaseType<__detail_type::name ## _v>


template <const char *&value, unsigned int N, typename ...P>
struct TypeTemp : Type {
    static_assert((N == 0 && sizeof...(P) > 0)|| (N > 0 && sizeof...(P) == N), "mismatched size");
};

#define DEFINE_TYPE_TEMP(name, size) \
namespace __detail_type { \
    const char * name ## _v = #name; \
} \
template<typename ...T> using name ## _t = TypeTemp<__detail_type::name ## _v, size, T...>

template <typename R, typename ... P>
struct Func_t : Type {};

DEFINE_TYPE(Int32);
DEFINE_TYPE(Int);
DEFINE_TYPE(String);
DEFINE_TYPE(Error);
DEFINE_TYPE(Byte);
DEFINE_TYPE(Boolean);
DEFINE_TYPE(Uint64);
DEFINE_TYPE(Int64);
DEFINE_TYPE(Uint32);
DEFINE_TYPE(Int16);
DEFINE_TYPE(Uint16);
DEFINE_TYPE(Float);
DEFINE_TYPE(Void);


DEFINE_TYPE_TEMP(Array, 1);
DEFINE_TYPE_TEMP(Map, 2);
DEFINE_TYPE_TEMP(Tuple, 0);
DEFINE_TYPE_TEMP(Option, 1);



template <typename T>
struct TypeFactory {
    std::unique_ptr<TypeNode> operator()() const {
        return nullptr;
    }
};

template <const char *&Name>
struct TypeFactory<BaseType<Name>> {
    std::unique_ptr<TypeNode> operator()() const {
        return std::unique_ptr<TypeNode>(new BaseTypeNode({0, 1}, std::string(Name)));
    }
};

std::unique_ptr<ReifiedTypeNode> addElement(std::unique_ptr<ReifiedTypeNode> &&type) {
    return std::move(type);
}

template <typename First, typename ... E>
std::unique_ptr<ReifiedTypeNode> addElement(std::unique_ptr<ReifiedTypeNode> &&type, First &&, E&& ...rest) {
    auto e = TypeFactory<First>{}();
    type->addElementTypeNode(e.release());
    return addElement(std::move(type), std::forward<E>(rest)...);
}

template <const char *&Name, unsigned int N, typename ...P>
struct TypeFactory<TypeTemp<Name, N, P...>> {
    std::unique_ptr<TypeNode> operator()() const {
        std::unique_ptr<ReifiedTypeNode> reified(
                new ReifiedTypeNode(new BaseTypeNode({0, 1}, std::string(Name))));
        return addElement(std::move(reified), P()...);
    }
};

std::unique_ptr<FuncTypeNode> addParam(std::unique_ptr<FuncTypeNode> &&type) {
    return std::move(type);
}

template <typename First, typename ... P>
std::unique_ptr<FuncTypeNode> addParam(std::unique_ptr<FuncTypeNode> &&type, First &&, P && ...rest) {
    auto e = TypeFactory<First>{}();
    type->addParamTypeNode(e.release());
    return addParam(std::move(type), std::forward<P>(rest)...);
}

template <typename R, typename ...P>
struct TypeFactory<Func_t<R, P...>> {
    std::unique_ptr<TypeNode> operator()() const {
        auto ret = TypeFactory<R>{}();
        std::unique_ptr<FuncTypeNode> func(new FuncTypeNode(0, ret.release()));
        return addParam(std::move(func), P()...);
    }
};


class TypeTest : public ::testing::Test {
public:
    SymbolTable pool;

public:
    TypeTest() = default;

    virtual ~TypeTest() = default;

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    virtual void assertTypeName(const char *typeName, DSType &type) {
        SCOPED_TRACE("");

        std::string name(typeName);
        // assert type name
        ASSERT_EQ(name, this->pool.getTypeName(type));

        // assert type
        ASSERT_TRUE(type == *this->pool.getType(name));
    }

    virtual void assertSuperType(DSType &type, DSType &superType) {
        SCOPED_TRACE("");

        DSType *actualSuperType = type.getSuperType();
        ASSERT_TRUE(actualSuperType != nullptr);
        ASSERT_STREQ(this->pool.getTypeName(*actualSuperType), this->pool.getTypeName(superType));
        ASSERT_TRUE(*actualSuperType == superType);
    }

    virtual void assertAttribute(flag8_set_t set, DSType &type) {
        SCOPED_TRACE("");
        ASSERT_EQ(hasFlag(set, DSType::EXTENDIBLE), type.isExtendible());
        ASSERT_EQ(hasFlag(set, DSType::VOID_TYPE), type.isVoidType());
        ASSERT_EQ(hasFlag(set, DSType::FUNC_TYPE), type.isFuncType());
        ASSERT_EQ(hasFlag(set, DSType::RECORD_TYPE), type.isRecordType());
    }

    virtual void assertAlias(const char *aliasName, DSType &type) {
        SCOPED_TRACE("");

        std::string name(aliasName);
        ASSERT_NE(name, this->pool.getTypeName(type));

        this->pool.setAlias(name, type);
        ASSERT_TRUE(this->pool.getType(name) != nullptr);
        ASSERT_TRUE(*this->pool.getType(name) == type);
    }

    virtual void assertTemplateName(const char *templateName, const TypeTemplate &t, unsigned int size) {
        SCOPED_TRACE("");

        std::string name(templateName);
        ASSERT_EQ(name, t.getName());

        auto &gotten = this->pool.getTypeTemplate(name);
        ASSERT_EQ(size, t.getElementTypeSize());
        ASSERT_EQ(size, gotten.getElementTypeSize());
        ASSERT_TRUE((unsigned long)&this->pool.getTypeTemplate(name) == (unsigned long)&t);
    }

    template <typename T>
    DSType &toType() {
        auto t = TypeFactory<T>{}();
        return TypeGenerator(this->pool).toType(*t);
    }
};


TEST_F(TypeTest, builtinName) {
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Any", this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Void", this->pool.get(TYPE::Void)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Nothing", this->pool.get(TYPE::Nothing)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Byte", this->pool.get(TYPE::Byte)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int16", this->pool.get(TYPE::Int16)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Uint16", this->pool.get(TYPE::Uint16)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int32", this->pool.get(TYPE::Int32)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Uint32", this->pool.get(TYPE::Uint32)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int64", this->pool.get(TYPE::Int64)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Uint64", this->pool.get(TYPE::Uint64)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int32", this->pool.get(TYPE::Int32)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Boolean", this->pool.get(TYPE::Boolean)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Float", this->pool.get(TYPE::Float)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("String", this->pool.get(TYPE::String)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("UnixFD", this->pool.get(TYPE::UnixFD)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Array<String>", this->pool.get(TYPE::StringArray)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Error", this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("ArithmeticError", this->pool.get(TYPE::ArithmeticError)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("OutOfRangeError", this->pool.get(TYPE::OutOfRangeError)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("KeyNotFoundError", this->pool.get(TYPE::KeyNotFoundError)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("TypeCastError", this->pool.get(TYPE::TypeCastError)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("StackOverflowError", this->pool.get(TYPE::StackOverflowError)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Regex", this->pool.get(TYPE::Regex)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("RegexSyntaxError", this->pool.get(TYPE::RegexSyntaxError)));
}

TEST_F(TypeTest, superType) {
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.get(TYPE::Any).getSuperType() != nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.get(TYPE::Void).getSuperType() == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.get(TYPE::Nothing).getSuperType() == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->toType<Option_t<String_t>>().getSuperType() == nullptr));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Any), this->pool.get(TYPE::_Root)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::_Value), this->pool.get(TYPE::Any)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Byte), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Int16), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Uint16), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Int32), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Uint32), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Int64), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Uint64), this->pool.get(TYPE::_Value)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Boolean), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Float), this->pool.get(TYPE::_Value)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::String), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::UnixFD), this->pool.get(TYPE::Any)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::StringArray), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Error), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::ArithmeticError), this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::OutOfRangeError), this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::KeyNotFoundError), this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::TypeCastError), this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::StackOverflowError), this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Regex), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::RegexSyntaxError), this->pool.get(TYPE::Error)));
}

TEST_F(TypeTest, attribute) {
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::VOID_TYPE, this->pool.get(TYPE::Void)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::NOTHING_TYPE, this->pool.get(TYPE::Nothing)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Byte)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Int16)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Uint16)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Int32)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Uint32)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Int64)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Uint64)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Boolean)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Float)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::String)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::UnixFD)));

    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::StringArray)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::ArithmeticError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::OutOfRangeError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::KeyNotFoundError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::TypeCastError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::StackOverflowError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.get(TYPE::RegexSyntaxError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.get(TYPE::Regex)));

    ASSERT_NO_FATAL_FAILURE(
            this->assertAttribute(DSType::FUNC_TYPE, this->toType<Func_t<Int32_t>>()));
}

TEST_F(TypeTest, alias) {
    ASSERT_NO_FATAL_FAILURE(this->assertAlias("Int", this->pool.get(TYPE::Int32)));
    ASSERT_NO_FATAL_FAILURE(this->assertAlias("Int_2", *this->pool.getType(std::string("Int"))));
}

TEST_F(TypeTest, templateName) {
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Array", this->pool.getArrayTemplate(), 1));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Map", this->pool.getMapTemplate(), 2));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Tuple", this->pool.getTupleTemplate(), 0));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Option", this->pool.getOptionTemplate(), 0));
}

TEST_F(TypeTest, typeToken) {
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Int32_t>(), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertAlias("Int", this->pool.get(TYPE::Int32)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Int_t>(), this->pool.get(TYPE::_Value)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Array_t<String_t>>(), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Array_t<Error_t>>(), this->pool.get(TYPE::Any)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Map_t<Byte_t, Uint64_t>>(), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Map_t<Boolean_t, Tuple_t<Uint32_t, String_t>>>(), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Tuple_t<Error_t>>(), this->pool.get(TYPE::Any)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Func_t<Void_t>>(), this->pool.get(TYPE::Func)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Func_t<Int16_t, Uint16_t, Int64_t, Float_t>>(), this->pool.get(TYPE::Func)));
}

TEST_F(TypeTest, pool) {
    auto &t = this->toType<Array_t<Int32_t>>();
    std::string typeName = this->pool.getTypeName(t);
    std::string alias = "IArray";
    ASSERT_NO_FATAL_FAILURE(this->assertAlias(alias.c_str(), t));
//    this->pool.abort();
//
//    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.getType(typeName) == nullptr));
//    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.getType(alias) == nullptr));
}

TEST_F(TypeTest, api) {
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->pool.get(TYPE::Any).isSameOrBaseTypeOf(this->pool.get(TYPE::String))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_FALSE(this->pool.get(TYPE::Any).isSameOrBaseTypeOf(this->pool.get(TYPE::Void))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->pool.get(TYPE::Boolean).isSameOrBaseTypeOf(this->pool.get(TYPE::Nothing))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_FALSE(this->pool.get(TYPE::Nothing).isSameOrBaseTypeOf(this->pool.get(TYPE::Boolean))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->pool.get(TYPE::Void).isSameOrBaseTypeOf(this->pool.get(TYPE::Nothing))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->toType<Option_t<Int32_t>>().isSameOrBaseTypeOf(this->pool.get(TYPE::Nothing))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_FALSE(this->pool.get(TYPE::Any).isSameOrBaseTypeOf(this->toType<Option_t<Int32_t>>())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->toType<Option_t<Int32_t>>().isSameOrBaseTypeOf(this->pool.get(TYPE::Int32))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->toType<Option_t<Error_t>>().isSameOrBaseTypeOf(this->pool.get(TYPE::ArithmeticError))));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}