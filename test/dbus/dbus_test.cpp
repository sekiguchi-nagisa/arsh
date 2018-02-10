#include "gtest/gtest.h"

#include <dbus/dbus_bind.h>

using namespace ydsh;

class DescriptorTest : public ::testing::Test {
public:
    SymbolTable *pool;
    BaseTypeDescriptorMap map;
    DescriptorBuilder builder;

    DescriptorTest() :
            pool(new SymbolTable()), map(this->pool), builder(this->pool, &this->map) {
    }

    virtual ~DescriptorTest() {
        delete this->pool;
        this->pool = 0;
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    virtual DSType &newArrayType(DSType &elementType) {
        SCOPED_TRACE("");

        std::vector<DSType *> types(1);
        types[0] = &elementType;

        return this->pool->createReifiedType(
                this->pool->getArrayTemplate(), std::move(types));
    }

    virtual DSType &newMapType(DSType &keyType, DSType &valueType) {
        SCOPED_TRACE("");

        std::vector<DSType *> types(2);
        types[0] = &keyType;
        types[1] = &valueType;

        return this->pool->createReifiedType(
                this->pool->getMapTemplate(), std::move(types));
    }

    virtual DSType &newTupleType(DSType &type1) {
        std::vector<DSType *> types(1);
        types[0] = &type1;
        return this->newTupleType(types);
    }

    virtual DSType &newTupleType(DSType &type1, DSType &type2) {
        std::vector<DSType *> types(2);
        types[0] = &type1;
        types[1] = &type2;
        return this->newTupleType(types);
    }

    virtual DSType &newTupleType(DSType &type1, DSType &type2, DSType &type3) {
        std::vector<DSType *> types(3);
        types[0] = &type1;
        types[1] = &type2;
        types[2] = &type3;
        return this->newTupleType(types);
    }


    virtual DSType &newTupleType(const std::vector<DSType *> &types) {
        return this->pool->createTupleType(std::vector<DSType *>(types));
    }



    virtual void assertDesc(const char *expected, DSType &type) {
        const char *actual = this->builder.buildDescriptor(type);
        ASSERT_STREQ(expected, actual);
    }
};

TEST_F(DescriptorTest, baseType) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertDesc("y", this->pool->getByteType());
        this->assertDesc("b", this->pool->getBooleanType());
        this->assertDesc("n", this->pool->getInt16Type());
        this->assertDesc("q", this->pool->getUint16Type());
        this->assertDesc("i", this->pool->getInt32Type());
        this->assertDesc("u", this->pool->getUint32Type());
        this->assertDesc("x", this->pool->getInt64Type());
        this->assertDesc("t", this->pool->getUint64Type());
        this->assertDesc("d", this->pool->getFloatType());

        this->assertDesc("s", this->pool->getStringType());
        this->assertDesc("o", this->pool->getObjectPathType());

        this->assertDesc("h", this->pool->getUnixFDType());
    });
}

TEST_F(DescriptorTest, containerType) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertDesc("v", this->pool->getVariantType());
        this->assertDesc("as", this->pool->getStringArrayType());
        this->assertDesc("a{si}", this->newMapType(this->pool->getStringType(),
                                                   this->pool->getInt32Type()));
        this->assertDesc("a{iay}", this->newMapType(
                this->pool->getInt32Type(), this->newArrayType(this->pool->getByteType())));

        this->assertDesc("aa{ob}", this->newArrayType(
                this->newMapType(this->pool->getObjectPathType(), this->pool->getBooleanType())));

        this->assertDesc("(x)", this->newTupleType(this->pool->getInt64Type()));
        this->assertDesc("(daq)", this->newTupleType(
                this->pool->getFloatType(), this->newArrayType(this->pool->getUint16Type())));
        this->assertDesc("(a{sv}aas(n))", this->newTupleType(
                this->newMapType(this->pool->getStringType(), this->pool->getVariantType()),
                this->newArrayType(this->newArrayType(this->pool->getStringType())),
                this->newTupleType(this->pool->getInt16Type())));
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}