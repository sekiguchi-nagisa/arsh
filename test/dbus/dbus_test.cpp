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

        this->assertDesc("y", this->pool->get(TYPE::Byte));
        this->assertDesc("b", this->pool->get(TYPE::Boolean));
        this->assertDesc("n", this->pool->get(TYPE::Int16));
        this->assertDesc("q", this->pool->get(TYPE::Uint16));
        this->assertDesc("i", this->pool->get(TYPE::Int32));
        this->assertDesc("u", this->pool->get(TYPE::Uint32));
        this->assertDesc("x", this->pool->get(TYPE::Int64));
        this->assertDesc("t", this->pool->get(TYPE::Uint64));
        this->assertDesc("d", this->pool->get(TYPE::Float));

        this->assertDesc("s", this->pool->get(TYPE::String));
        this->assertDesc("o", this->pool->get(TYPE::ObjectPath));

        this->assertDesc("h", this->pool->get(TYPE::UnixFD));
    });
}

TEST_F(DescriptorTest, containerType) {
    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->assertDesc("v", this->pool->get(TYPE::Variant));
        this->assertDesc("as", this->pool->get(TYPE::StringArray));
        this->assertDesc("a{si}", this->newMapType(this->pool->get(TYPE::String),
                                                   this->pool->get(TYPE::Int32)));
        this->assertDesc("a{iay}", this->newMapType(
                this->pool->get(TYPE::Int32), this->newArrayType(this->pool->get(TYPE::Byte))));

        this->assertDesc("aa{ob}", this->newArrayType(
                this->newMapType(this->pool->get(TYPE::ObjectPath), this->pool->get(TYPE::Boolean))));

        this->assertDesc("(x)", this->newTupleType(this->pool->get(TYPE::Int64)));
        this->assertDesc("(daq)", this->newTupleType(
                this->pool->get(TYPE::Float), this->newArrayType(this->pool->get(TYPE::Uint16))));
        this->assertDesc("(a{sv}aas(n))", this->newTupleType(
                this->newMapType(this->pool->get(TYPE::String), this->pool->get(TYPE::Variant)),
                this->newArrayType(this->newArrayType(this->pool->get(TYPE::String))),
                this->newTupleType(this->pool->get(TYPE::Int16))));
    });
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}