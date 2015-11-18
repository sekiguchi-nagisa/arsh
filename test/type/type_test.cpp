#include <gtest/gtest.h>

#include <core/TypePool.h>
#include <core/DSType.h>
#include <parser/TypeChecker.h>

using namespace ydsh::core;
using namespace ydsh::ast;

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
            new ReifiedTypeNode(new BaseTypeNode(0, std::string(name))));
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
    std::unique_ptr<FuncTypeNode> func(new FuncTypeNode(returnType.release()));
    return addParamType(std::move(func), std::forward<T>(paramTypes)...);
}

inline std::unique_ptr<TypeNode> type(const char *name, unsigned int lineNum = 0) {
    return std::unique_ptr<TypeNode>(new BaseTypeNode(lineNum, std::string(name)));
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
        SCOPED_TRACE("");
        TypeNode *ptr = tok.get();
        return TypeChecker::TypeGenerator(this->pool).generateTypeAndThrow(ptr);
    }
};


TEST_F(TypeTest, builtinName) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertTypeName("Any", this->pool.getAnyType());
        this->assertTypeName("Void", this->pool.getVoidType());

        this->assertTypeName("Byte", this->pool.getByteType());
        this->assertTypeName("Int16", this->pool.getInt16Type());
        this->assertTypeName("Uint16", this->pool.getUint16Type());
        this->assertTypeName("Int32", this->pool.getInt32Type());
        this->assertTypeName("Uint32", this->pool.getUint32Type());
        this->assertTypeName("Int64", this->pool.getInt64Type());
        this->assertTypeName("Uint64", this->pool.getUint64Type());

        this->assertTypeName("Int32", this->pool.getIntType());

        this->assertTypeName("Boolean", this->pool.getBooleanType());
        this->assertTypeName("Float", this->pool.getFloatType());

        this->assertTypeName("String", this->pool.getStringType());
        this->assertTypeName("ObjectPath", this->pool.getObjectPathType());
        this->assertTypeName("UnixFD", this->pool.getUnixFDType());
        this->assertTypeName("Variant", this->pool.getVariantType());

        this->assertTypeName("Array<String>", this->pool.getStringArrayType());
        this->assertTypeName("Error", this->pool.getErrorType());
        this->assertTypeName("ArithmeticError", this->pool.getArithmeticErrorType());
        this->assertTypeName("OutOfRangeError", this->pool.getOutOfRangeErrorType());
        this->assertTypeName("KeyNotFoundError", this->pool.getKeyNotFoundErrorType());
        this->assertTypeName("TypeCastError", this->pool.getTypeCastErrorType());
        this->assertTypeName("DBusError", this->pool.getDBusErrorType());
    });
}

TEST_F(TypeTest, superType) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        ASSERT_TRUE(this->pool.getAnyType().getSuperType() == nullptr);
        ASSERT_TRUE(this->pool.getVoidType().getSuperType() == nullptr);

        this->assertSuperType(this->pool.getVariantType(), this->pool.getAnyType());
        this->assertSuperType(this->pool.getValueType(), this->pool.getVariantType());

        this->assertSuperType(this->pool.getByteType(), this->pool.getValueType());
        this->assertSuperType(this->pool.getInt16Type(), this->pool.getValueType());
        this->assertSuperType(this->pool.getUint16Type(), this->pool.getValueType());
        this->assertSuperType(this->pool.getInt32Type(), this->pool.getValueType());
        this->assertSuperType(this->pool.getUint32Type(), this->pool.getValueType());
        this->assertSuperType(this->pool.getInt64Type(), this->pool.getValueType());
        this->assertSuperType(this->pool.getUint64Type(), this->pool.getValueType());

        this->assertSuperType(this->pool.getBooleanType(), this->pool.getValueType());
        this->assertSuperType(this->pool.getFloatType(), this->pool.getValueType());

        this->assertSuperType(this->pool.getStringType(), this->pool.getValueType());
        this->assertSuperType(this->pool.getObjectPathType(), this->pool.getValueType());
        this->assertSuperType(this->pool.getUnixFDType(), this->pool.getUint32Type());    //FIXME:

        this->assertSuperType(this->pool.getStringArrayType(), this->pool.getVariantType());
        this->assertSuperType(this->pool.getErrorType(), this->pool.getAnyType());
        this->assertSuperType(this->pool.getArithmeticErrorType(), this->pool.getErrorType());
        this->assertSuperType(this->pool.getOutOfRangeErrorType(), this->pool.getErrorType());
        this->assertSuperType(this->pool.getKeyNotFoundErrorType(), this->pool.getErrorType());
        this->assertSuperType(this->pool.getTypeCastErrorType(), this->pool.getErrorType());
        this->assertSuperType(this->pool.getDBusErrorType(), this->pool.getErrorType());
    });
}

TEST_F(TypeTest, alias) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->assertAlias("Int", this->pool.getInt32Type());
        this->assertAlias("Int_2", *this->pool.getType(std::string("Int")));
    });
}

TEST_F(TypeTest, templateName) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertTemplateName("Array", this->pool.getArrayTemplate(), 1);
        this->assertTemplateName("Map", this->pool.getMapTemplate(), 2);
        this->assertTemplateName("Tuple", this->pool.getTupleTemplate(), 0);
    });
}

TEST_F(TypeTest, typeToken) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertSuperType(this->toType(type("Int32")), this->pool.getValueType());
        this->assertAlias("Int", this->pool.getInt32Type());
        this->assertSuperType(this->toType(type("Int")), this->pool.getValueType());

        this->assertSuperType(this->toType(array(type("String"))), this->pool.getVariantType());
        this->assertSuperType(this->toType(array(reified("Array", type("ObjectPath")))), this->pool.getVariantType());
        this->assertSuperType(this->toType(array(type("Error"))), this->pool.getAnyType());

        this->assertSuperType(this->toType(map(type("Byte"), type("Uint64"))), this->pool.getVariantType());
        this->assertSuperType(this->toType(
                map(type("Boolean"), tuple(type("Uint32"), type("String")))), this->pool.getVariantType());
        this->assertSuperType(this->toType(tuple(type("Error"))), this->pool.getAnyType());

        this->assertSuperType(this->toType(func(type("Void"))), this->pool.getBaseFuncType());
        this->assertSuperType(this->toType(
                func(type("Int16"), type("Uint16"), type("Int64"), type("Float"))), this->pool.getBaseFuncType());
    });
}

TEST_F(TypeTest, pool) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        // type
        auto &t = this->toType(array(type("Int32")));
        std::string typeName = this->pool.getTypeName(t);
        std::string alias = "IArray";
        this->assertAlias(alias.c_str(), t);
        this->pool.abort();

        ASSERT_TRUE(this->pool.getType(typeName) == nullptr);
        ASSERT_TRUE(this->pool.getType(alias) == nullptr);
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}