#include <gtest/gtest.h>

#include <core/TypePool.h>
#include <core/DSType.h>
#include <core/TypeTemplate.h>
#include <ast/TypeToken.h>
#include <parser/TypeChecker.h>

using namespace ydsh::core;
using namespace ydsh::ast;

class TypeTest : public ::testing::Test {
public:
    char **env;
    TypePool *pool;

public:
    TypeTest() : env(0), pool(0) {
        static char env1[] = "HOME";
        static char env2[] = "PATH";

        this->env = new char*[2];
        this->env[0] = env1;
        this->env[1] = env2;

        this->pool = new TypePool(this->env);
    }

    virtual ~TypeTest() {
        delete[] this->env;
        this->env = 0;

        delete this->pool;
        this->pool = 0;
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    virtual void assertTypeName(const char *typeName, DSType *type) {
        SCOPED_TRACE("");

        std::string name(typeName);
        // assert type name
        ASSERT_EQ(name, this->pool->getTypeName(*type));

        // assert type
        ASSERT_TRUE(*type == *this->pool->getType(name));
    }

    virtual void assertSuperType(DSType *type, DSType *superType) {
        SCOPED_TRACE("");

        ASSERT_TRUE(type != nullptr);
        ASSERT_TRUE(superType != nullptr);
        DSType *actualSuperType = type->getSuperType();
        ASSERT_TRUE(actualSuperType != nullptr);
        ASSERT_EQ(this->pool->getTypeName(*actualSuperType), this->pool->getTypeName(*superType));
        ASSERT_TRUE(*actualSuperType == *superType);
    }

    virtual void assertAlias(const char *aliasName, DSType *type) {
        SCOPED_TRACE("");

        std::string name(aliasName);
        ASSERT_NE(name, this->pool->getTypeName(*type));

        this->pool->setAlias(name, type);
        ASSERT_TRUE(this->pool->getType(name) != nullptr);
        ASSERT_TRUE(*this->pool->getType(name) == *type);
    }

    virtual void assertTemplateName(const char *templateName, TypeTemplate *t, unsigned int size) {
        SCOPED_TRACE("");

        std::string name(templateName);
        ASSERT_EQ(name, t->getName());

        TypeTemplate *gotten = this->pool->getTypeTemplate(name);
        ASSERT_EQ(size, t->getElementTypeSize());
        ASSERT_EQ(size, gotten->getElementTypeSize());
        ASSERT_TRUE((unsigned long)this->pool->getTypeTemplate(name) == (unsigned long)t);
    }

    virtual DSType *toType(std::unique_ptr<TypeToken> &&tok) {
        SCOPED_TRACE("");
        try {
            TypeToken *ptr = tok.get();
            return TypeGenerator(this->pool).generateTypeAndThrow(ptr);
        } catch(const TypeCheckError &e) {
            std::cerr << e.getTemplate() << std::endl;
            return nullptr;
        }
    }
};

// helper utils
#include "type_util.hpp"

static std::unique_ptr<TypeToken> type(const char *name, unsigned int lineNum = 0) {
    return std::unique_ptr<TypeToken>(new ClassTypeToken(lineNum, std::string(name)));
}


TEST_F(TypeTest, builtinName) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertTypeName("Any", this->pool->getAnyType());
        this->assertTypeName("Void", this->pool->getVoidType());

        this->assertTypeName("Byte", this->pool->getByteType());
        this->assertTypeName("Int16", this->pool->getInt16Type());
        this->assertTypeName("Uint16", this->pool->getUint16Type());
        this->assertTypeName("Int32", this->pool->getInt32Type());
        this->assertTypeName("Uint32", this->pool->getUint32Type());
        this->assertTypeName("Int64", this->pool->getInt64Type());
        this->assertTypeName("Uint64", this->pool->getUint64Type());

        this->assertTypeName("Int32", this->pool->getIntType());

        this->assertTypeName("Boolean", this->pool->getBooleanType());
        this->assertTypeName("Float", this->pool->getFloatType());

        this->assertTypeName("String", this->pool->getStringType());
        this->assertTypeName("ObjectPath", this->pool->getObjectPathType());
        this->assertTypeName("UnixFD", this->pool->getUnixFDType());
        this->assertTypeName("Variant", this->pool->getVariantType());

        this->assertTypeName("Array<String>", this->pool->getStringArrayType());
        this->assertTypeName("Error", this->pool->getErrorType());
        this->assertTypeName("ArithmeticError", this->pool->getArithmeticErrorType());
        this->assertTypeName("OutOfIndexError", this->pool->getOutOfIndexErrorType());
        this->assertTypeName("KeyNotFoundError", this->pool->getKeyNotFoundErrorType());
        this->assertTypeName("TypeCastError", this->pool->getTypeCastErrorType());
    });
}

TEST_F(TypeTest, superType) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        ASSERT_TRUE(this->pool->getAnyType()->getSuperType() == nullptr);
        ASSERT_TRUE(this->pool->getVoidType()->getSuperType() == nullptr);

        this->assertSuperType(this->pool->getVariantType(), this->pool->getAnyType());
        this->assertSuperType(this->pool->getValueType(), this->pool->getVariantType());

        this->assertSuperType(this->pool->getByteType(), this->pool->getValueType());
        this->assertSuperType(this->pool->getInt16Type(), this->pool->getValueType());
        this->assertSuperType(this->pool->getUint16Type(), this->pool->getValueType());
        this->assertSuperType(this->pool->getInt32Type(), this->pool->getValueType());
        this->assertSuperType(this->pool->getUint32Type(), this->pool->getValueType());
        this->assertSuperType(this->pool->getInt64Type(), this->pool->getValueType());
        this->assertSuperType(this->pool->getUint64Type(), this->pool->getValueType());

        this->assertSuperType(this->pool->getBooleanType(), this->pool->getValueType());
        this->assertSuperType(this->pool->getFloatType(), this->pool->getValueType());

        this->assertSuperType(this->pool->getStringType(), this->pool->getValueType());
        this->assertSuperType(this->pool->getObjectPathType(), this->pool->getValueType());
        this->assertSuperType(this->pool->getUnixFDType(), this->pool->getUint32Type());    //FIXME:

        this->assertSuperType(this->pool->getStringArrayType(), this->pool->getVariantType());
        this->assertSuperType(this->pool->getErrorType(), this->pool->getAnyType());
        this->assertSuperType(this->pool->getArithmeticErrorType(), this->pool->getErrorType());
        this->assertSuperType(this->pool->getOutOfIndexErrorType(), this->pool->getErrorType());
        this->assertSuperType(this->pool->getKeyNotFoundErrorType(), this->pool->getErrorType());
        this->assertSuperType(this->pool->getTypeCastErrorType(), this->pool->getErrorType());
    });
}

TEST_F(TypeTest, alias) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");
        this->assertAlias("Int", this->pool->getInt32Type());
        this->assertAlias("Int_2", this->pool->getType(std::string("Int")));
    });
}

TEST_F(TypeTest, templateName) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertTemplateName("Array", this->pool->getArrayTemplate(), 1);
        this->assertTemplateName("Map", this->pool->getMapTemplate(), 2);
        this->assertTemplateName("Tuple", this->pool->getTupleTemplate(), 0);
    });
}

TEST_F(TypeTest, typeToken) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertSuperType(this->toType(type("Int32")), this->pool->getValueType());
        this->assertAlias("Int", this->pool->getInt32Type());
        this->assertSuperType(this->toType(type("Int")), this->pool->getValueType());

        this->assertSuperType(this->toType(reified("Array", type("String"))), this->pool->getVariantType());
        this->assertSuperType(this->toType(reified("Array", reified("Array", type("ObjectPath")))), this->pool->getVariantType());
        this->assertSuperType(this->toType(reified("Array", type("Error"))), this->pool->getAnyType());

        this->assertSuperType(this->toType(reified("Map", type("Byte"), type("Uint64"))), this->pool->getVariantType());
        this->assertSuperType(this->toType(
                reified("Map", type("Boolean"),
                        reified("Tuple", type("Uint32"), type("String")))), this->pool->getVariantType());
        this->assertSuperType(this->toType(reified("Tuple", type("Error"))), this->pool->getAnyType());

        this->assertSuperType(this->toType(func(type("Void"))), this->pool->getBaseFuncType());
        this->assertSuperType(this->toType(
                func(type("Int16"), type("Uint16"), type("Int64"), type("Float"))), this->pool->getBaseFuncType());
    });
}