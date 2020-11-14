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

DEFINE_TYPE(Int);
DEFINE_TYPE(String);
DEFINE_TYPE(Error);
DEFINE_TYPE(Boolean);
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

static void addElement(std::vector<std::unique_ptr<TypeNode>> &) {}

template <typename First, typename ... E>
void addElement(std::vector<std::unique_ptr<TypeNode>> &types, First &&, E&& ...rest) {
    auto e = TypeFactory<First>{}();
    types.push_back(std::move(e));
    addElement(types, std::forward<E>(rest)...);
}

template <const char *&Name, unsigned int N, typename ...P>
struct TypeFactory<TypeTemp<Name, N, P...>> {
    std::unique_ptr<TypeNode> operator()() const {
        std::vector<std::unique_ptr<TypeNode>> types;
        addElement(types, P()...);
        return std::make_unique<ReifiedTypeNode>(
                std::make_unique<BaseTypeNode>(Token{0, 1}, std::string(Name)),
                std::move(types), Token{0, 0});
    }
};

static void addParam(std::vector<std::unique_ptr<TypeNode>> &) {}

template <typename First, typename ... P>
void addParam(std::vector<std::unique_ptr<TypeNode>> &types, First &&, P && ...rest) {
    auto e = TypeFactory<First>{}();
    types.push_back(std::move(e));
    addParam(types, std::forward<P>(rest)...);
}

template <typename R, typename ...P>
struct TypeFactory<Func_t<R, P...>> {
    std::unique_ptr<TypeNode> operator()() const {
        auto ret = TypeFactory<R>{}();
        std::vector<std::unique_ptr<TypeNode>> types;
        addParam(types, P()...);
        return std::make_unique<FuncTypeNode>(0, std::move(ret), std::move(types), Token {0,0});
    }
};


class TypeTest : public ::testing::Test {
public:
    ModuleLoader loader;
    SymbolTable symbolTable;
    TypePool pool;
    TypeChecker checker;

public:
    TypeTest() : symbolTable(this->loader), checker(this->pool, this->symbolTable, false, nullptr) {}

    virtual void assertTypeName(const char *typeName, DSType &type) {
        std::string name(typeName);
        // assert type name
        ASSERT_EQ(name, type.getName());

        // assert type
        auto ret = this->pool.getType(name);
        ASSERT_TRUE(ret);
        ASSERT_TRUE(type == *ret.take());
    }

    virtual void assertSuperType(DSType &type, DSType &superType) {
        auto *actualSuperType = type.getSuperType();
        ASSERT_TRUE(actualSuperType != nullptr);
        ASSERT_STREQ(actualSuperType->getName(), superType.getName());
        ASSERT_TRUE(*actualSuperType == superType);
    }

    virtual void assertAttribute(TypeAttr set, DSType &type) {
        ASSERT_EQ(hasFlag(set, TypeAttr::EXTENDIBLE), type.isExtendible());
        ASSERT_EQ(hasFlag(set, TypeAttr::FUNC_TYPE), type.isFuncType());
        ASSERT_EQ(hasFlag(set, TypeAttr::RECORD_TYPE), type.isRecordType());
    }

    virtual void assertTemplateName(const char *templateName, const TypeTemplate &t, unsigned int size) {
        std::string name(templateName);
        ASSERT_EQ(name, t.getName());

        auto ret = this->pool.getTypeTemplate(name);
        ASSERT_TRUE(ret);
        auto gotten = ret.take();
        ASSERT_EQ(size, t.getElementTypeSize());
        ASSERT_EQ(size, gotten->getElementTypeSize());
        ASSERT_TRUE(reinterpret_cast<uintptr_t>(this->pool.getTypeTemplate(name).take()) ==
                            reinterpret_cast<uintptr_t>(&t));
    }

    template <typename T>
    DSType &toType() {
        auto t = TypeFactory<T>{}();
        auto node = this->checker(nullptr, std::move(t));
        assert(node->is(NodeKind::TypeOp));
        return static_cast<TypeOpNode &>(*node).getExprNode().getType();
    }
};


TEST_F(TypeTest, builtinName) {
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Any", this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Void", this->pool.get(TYPE::Void)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Nothing", this->pool.get(TYPE::Nothing)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int", this->pool.get(TYPE::Int)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Boolean", this->pool.get(TYPE::Boolean)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Float", this->pool.get(TYPE::Float)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("String", this->pool.get(TYPE::String)));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("UnixFD", this->pool.get(TYPE::UnixFD)));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("[String]", this->pool.get(TYPE::StringArray)));
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
    ASSERT_TRUE(this->pool.get(TYPE::Any).getSuperType() != nullptr);
    ASSERT_TRUE(this->pool.get(TYPE::Void).getSuperType() == nullptr);
    ASSERT_TRUE(this->pool.get(TYPE::Nothing).getSuperType() == nullptr);
    ASSERT_TRUE(this->toType<Option_t<String_t>>().getSuperType() == nullptr);

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Any), this->pool.get(TYPE::_Root)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::_Value), this->pool.get(TYPE::Any)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Int), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Signal), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.get(TYPE::Signals), this->pool.get(TYPE::Any)));

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
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr(), this->pool.get(TYPE::Int)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr(), this->pool.get(TYPE::Boolean)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr(), this->pool.get(TYPE::Float)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr(), this->pool.get(TYPE::String)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr(), this->pool.get(TYPE::UnixFD)));

    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr(), this->pool.get(TYPE::StringArray)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::Error)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::ArithmeticError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::OutOfRangeError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::KeyNotFoundError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::TypeCastError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::StackOverflowError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr::EXTENDIBLE, this->pool.get(TYPE::RegexSyntaxError)));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(TypeAttr(), this->pool.get(TYPE::Regex)));

    ASSERT_NO_FATAL_FAILURE(
            this->assertAttribute(TypeAttr::FUNC_TYPE, this->toType<Func_t<Int_t>>()));
}

TEST_F(TypeTest, templateName) {
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Array", this->pool.getArrayTemplate(), 1));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Map", this->pool.getMapTemplate(), 2));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Tuple", this->pool.getTupleTemplate(), 0));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Option", this->pool.getOptionTemplate(), 0));
}

TEST_F(TypeTest, typeToken) {
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Int_t>(), this->pool.get(TYPE::_Value)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Int_t>(), this->pool.get(TYPE::_Value)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Array_t<String_t>>(), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Array_t<Error_t>>(), this->pool.get(TYPE::Any)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Map_t<Boolean_t, Tuple_t<Float_t, String_t>>>(), this->pool.get(TYPE::Any)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Tuple_t<Error_t>>(), this->pool.get(TYPE::Any)));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Func_t<Void_t>>(), this->pool.get(TYPE::Func)));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType<Func_t<String_t, Int_t, Float_t>>(), this->pool.get(TYPE::Func)));
}

TEST_F(TypeTest, api) {
    ASSERT_TRUE(this->pool.get(TYPE::Any).isSameOrBaseTypeOf(this->pool.get(TYPE::String)));
    ASSERT_FALSE(this->pool.get(TYPE::Any).isSameOrBaseTypeOf(this->pool.get(TYPE::Void)));
    ASSERT_TRUE(this->pool.get(TYPE::Boolean).isSameOrBaseTypeOf(this->pool.get(TYPE::Nothing)));
    ASSERT_FALSE(this->pool.get(TYPE::Nothing).isSameOrBaseTypeOf(this->pool.get(TYPE::Boolean)));
    ASSERT_TRUE(this->pool.get(TYPE::Void).isSameOrBaseTypeOf(this->pool.get(TYPE::Nothing)));
    ASSERT_TRUE(this->toType<Option_t<Int_t>>().isSameOrBaseTypeOf(this->pool.get(TYPE::Nothing)));
    ASSERT_FALSE(this->pool.get(TYPE::Any).isSameOrBaseTypeOf(this->toType<Option_t<Int_t>>()));
    ASSERT_TRUE(this->toType<Option_t<Int_t>>().isSameOrBaseTypeOf(this->pool.get(TYPE::Int)));
    ASSERT_TRUE(this->toType<Option_t<Error_t>>().isSameOrBaseTypeOf(this->pool.get(TYPE::ArithmeticError)));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}