#include <gtest/gtest.h>

#include <type.h>
#include <type_checker.h>

using namespace ydsh;

// helper method for type token generation
std::unique_ptr<TypeNode> addRestElements(std::unique_ptr<ReifiedTypeNode> &&reified) {
    return std::move(reified);
}

template <typename... T>
std::unique_ptr<TypeNode> addRestElements(std::unique_ptr<ReifiedTypeNode> &&reified,
                                          std::unique_ptr<TypeNode>&& type, T&&... rest) {
    reified->addElementTypeNode(type.release());
    return addRestElements(std::move(reified), std::forward<T>(rest)...);
}


template <typename... T>
std::unique_ptr<TypeNode> reified(const char *name, std::unique_ptr<TypeNode> &&first, T&&... rest) {
    std::unique_ptr<ReifiedTypeNode> reified(
            new ReifiedTypeNode(new BaseTypeNode({0, 1}, std::string(name))));
    reified->addElementTypeNode(first.release());
    return addRestElements(std::move(reified), std::forward<T>(rest)...);
}


std::unique_ptr<TypeNode> addParamType(std::unique_ptr<FuncTypeNode> &&func) {
    return std::move(func);
}

template <typename... T>
std::unique_ptr<TypeNode> addParamType(std::unique_ptr<FuncTypeNode> &&func,
                                       std::unique_ptr<TypeNode>&& type, T&&... rest) {
    func->addParamTypeNode(type.release());
    return addParamType(std::move(func), std::forward<T>(rest)...);
}

template <typename... T>
std::unique_ptr<TypeNode> func(std::unique_ptr<TypeNode> &&returnType, T&&... paramTypes) {
    std::unique_ptr<FuncTypeNode> func(new FuncTypeNode(0, returnType.release()));
    return addParamType(std::move(func), std::forward<T>(paramTypes)...);
}

inline std::unique_ptr<TypeNode> type(const char *name, unsigned int lineNum = 0) {
    return std::unique_ptr<TypeNode>(new BaseTypeNode({lineNum, 1}, std::string(name)));
}

inline std::unique_ptr<TypeNode> array(std::unique_ptr<TypeNode> &&type) {
    return reified("Array", std::move(type));
}

inline std::unique_ptr<TypeNode> map(std::unique_ptr<TypeNode> &&keyType, std::unique_ptr<TypeNode> &&valueType) {
    return reified("Map", std::move(keyType), std::move(valueType));
}

template <typename... T>
std::unique_ptr<TypeNode> tuple(std::unique_ptr<TypeNode> &&first, T&&... rest) {
    return reified("Tuple", std::move(first), std::forward<T>(rest)...);
}

inline std::unique_ptr<TypeNode> opt(std::unique_ptr<TypeNode> &&type) {
    return reified("Option", std::move(type));
}


class TypeTest : public ::testing::Test {
public:
    TypePool pool;

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
        ASSERT_EQ(this->pool.getTypeName(*actualSuperType), this->pool.getTypeName(superType));
        ASSERT_TRUE(*actualSuperType == superType);
    }

    virtual void assertAttribute(flag8_set_t set, DSType &type) {
        SCOPED_TRACE("");
        ASSERT_EQ(hasFlag(set, DSType::EXTENDIBLE), type.isExtendible());
        ASSERT_EQ(hasFlag(set, DSType::VOID_TYPE), type.isVoidType());
        ASSERT_EQ(hasFlag(set, DSType::FUNC_TYPE), type.isFuncType());
        ASSERT_EQ(hasFlag(set, DSType::IFACE_TYPE), type.isInterface());
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

    virtual DSType &toType(std::unique_ptr<TypeNode> &&tok) {
        TypeNode *ptr = tok.get();
        return TypeGenerator(this->pool).toType(*ptr);
    }
};


TEST_F(TypeTest, builtinName) {
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Any", this->pool.getAnyType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Void", this->pool.getVoidType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Bottom%%", this->pool.getBottomType()));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Byte", this->pool.getByteType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int16", this->pool.getInt16Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Uint16", this->pool.getUint16Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int32", this->pool.getInt32Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Uint32", this->pool.getUint32Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int64", this->pool.getInt64Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Uint64", this->pool.getUint64Type()));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Int32", this->pool.getIntType()));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Boolean", this->pool.getBooleanType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Float", this->pool.getFloatType()));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("String", this->pool.getStringType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("ObjectPath", this->pool.getObjectPathType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("UnixFD", this->pool.getUnixFDType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Variant", this->pool.getVariantType()));

    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Array<String>", this->pool.getStringArrayType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Error", this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("ArithmeticError", this->pool.getArithmeticErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("OutOfRangeError", this->pool.getOutOfRangeErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("KeyNotFoundError", this->pool.getKeyNotFoundErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("TypeCastError", this->pool.getTypeCastErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("StackOverflowError", this->pool.getStackOverflowErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("DBusError", this->pool.getDBusErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("Regex", this->pool.getRegexType()));
    ASSERT_NO_FATAL_FAILURE(this->assertTypeName("RegexSyntaxError", this->pool.getRegexSyntaxErrorType()));
}

TEST_F(TypeTest, superType) {
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.getAnyType().getSuperType() == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.getVoidType().getSuperType() == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->pool.getBottomType().getSuperType() == nullptr));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->toType(opt(type("String"))).getSuperType() == nullptr));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getVariantType(), this->pool.getAnyType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getValueType(), this->pool.getVariantType()));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getByteType(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getInt16Type(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getUint16Type(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getInt32Type(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getUint32Type(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getInt64Type(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getUint64Type(), this->pool.getValueType()));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getBooleanType(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getFloatType(), this->pool.getValueType()));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getStringType(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getObjectPathType(), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getUnixFDType(), this->pool.getUint32Type()));    //FIXME:

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getStringArrayType(), this->pool.getVariantType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getErrorType(), this->pool.getAnyType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getArithmeticErrorType(), this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getOutOfRangeErrorType(), this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getKeyNotFoundErrorType(), this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getTypeCastErrorType(), this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getStackOverflowErrorType(), this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getDBusErrorType(), this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getRegexType(), this->pool.getAnyType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->pool.getRegexSyntaxErrorType(), this->pool.getErrorType()));
}

TEST_F(TypeTest, attribute) {
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getAnyType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::VOID_TYPE, this->pool.getVoidType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::BOTTOM_TYPE, this->pool.getBottomType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getVariantType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getByteType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getInt16Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getUint16Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getInt32Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getUint32Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getInt64Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getUint64Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getBooleanType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getFloatType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getStringType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getObjectPathType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getUnixFDType()));

    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getStringArrayType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getArithmeticErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getOutOfRangeErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getKeyNotFoundErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getTypeCastErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getStackOverflowErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getDBusErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(DSType::EXTENDIBLE, this->pool.getRegexSyntaxErrorType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAttribute(0, this->pool.getRegexType()));

    ASSERT_NO_FATAL_FAILURE(
            this->assertAttribute(DSType::FUNC_TYPE, this->toType(func(type("Int32")))));
}

TEST_F(TypeTest, alias) {
    ASSERT_NO_FATAL_FAILURE(this->assertAlias("Int", this->pool.getInt32Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertAlias("Int_2", *this->pool.getType(std::string("Int"))));
}

TEST_F(TypeTest, templateName) {
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Array", this->pool.getArrayTemplate(), 1));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Map", this->pool.getMapTemplate(), 2));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Tuple", this->pool.getTupleTemplate(), 0));
    ASSERT_NO_FATAL_FAILURE(this->assertTemplateName("Option", this->pool.getOptionTemplate(), 0));
}

TEST_F(TypeTest, typeToken) {
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(type("Int32")), this->pool.getValueType()));
    ASSERT_NO_FATAL_FAILURE(this->assertAlias("Int", this->pool.getInt32Type()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(type("Int")), this->pool.getValueType()));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(array(type("String"))), this->pool.getVariantType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(array(reified("Array", type("ObjectPath")))), this->pool.getVariantType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(array(type("Error"))), this->pool.getAnyType()));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(map(type("Byte"), type("Uint64"))), this->pool.getVariantType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(
            map(type("Boolean"), tuple(type("Uint32"), type("String")))), this->pool.getVariantType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(tuple(type("Error"))), this->pool.getAnyType()));

    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(func(type("Void"))), this->pool.getBaseFuncType()));
    ASSERT_NO_FATAL_FAILURE(this->assertSuperType(this->toType(
            func(type("Int16"), type("Uint16"), type("Int64"), type("Float"))), this->pool.getBaseFuncType()));
}

TEST_F(TypeTest, pool) {
    auto &t = this->toType(array(type("Int32")));
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
            ASSERT_TRUE(this->pool.getAnyType().isSameOrBaseTypeOf(this->pool.getStringType())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_FALSE(this->pool.getAnyType().isSameOrBaseTypeOf(this->pool.getVoidType())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->pool.getBooleanType().isSameOrBaseTypeOf(this->pool.getBottomType())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_FALSE(this->pool.getBottomType().isSameOrBaseTypeOf(this->pool.getBooleanType())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->pool.getVoidType().isSameOrBaseTypeOf(this->pool.getBottomType())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->toType(opt(type("Int32"))).isSameOrBaseTypeOf(this->pool.getBottomType())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_FALSE(this->pool.getAnyType().isSameOrBaseTypeOf(this->toType(opt(type("Int32"))))));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->toType(opt(type("Int32"))).isSameOrBaseTypeOf(this->pool.getInt32Type())));
    ASSERT_NO_FATAL_FAILURE(
            ASSERT_TRUE(this->toType(opt(type("Error"))).isSameOrBaseTypeOf(this->pool.getArithmeticErrorType())));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}