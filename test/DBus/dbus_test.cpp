#include <gtest/gtest.h>

#include <dbus/DBusBind.h>
#include <core/TypeLookupError.h>

using namespace ydsh::core;

class DescriptorTest : public ::testing::Test {
public:
    char **env;
    TypePool *pool;
    BaseTypeDescriptorMap *map;
    DescriptorBuilder *builder;

    DescriptorTest() :  env(0), pool(0), map(0), builder(0) {
        static char env1[] = "HOME:hoge";
        static char env2[] = "PATH:/bin";

        this->env = new char*[2];
        this->env[0] = env1;
        this->env[1] = env2;

        this->pool = new TypePool(this->env);
        this->map = new BaseTypeDescriptorMap(this->pool);
        this->builder = new DescriptorBuilder(this->pool, this->map);
    }

    virtual ~DescriptorTest() {
        delete[] this->env;
        this->env = 0;

        delete this->pool;
        this->pool = 0;

        delete this->map;
        this->map = 0;

        delete this->builder;
        this->builder = 0;
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    virtual DSType *newArrayType(DSType *elementType) {
        SCOPED_TRACE("");

        std::vector<DSType *> types(1);
        types[0] = elementType;

        try {
            return this->pool->createAndGetReifiedTypeIfUndefined(
                    this->pool->getArrayTemplate(), std::move(types));
        } catch(const TypeLookupError &e) {
            return nullptr;
        }
    }

    virtual DSType *newMapType(DSType *keyType, DSType *valueType) {
        SCOPED_TRACE("");

        std::vector<DSType *> types(2);
        types[0] = keyType;
        types[1] = valueType;

        try {
            return this->pool->createAndGetReifiedTypeIfUndefined(
                    this->pool->getMapTemplate(), std::move(types));
        } catch(const TypeLookupError &e) {
            return nullptr;
        }
    }

    virtual DSType *newTupleType(DSType *type1) {
        std::vector<DSType *> types(1);
        types[0] = type1;
        return this->newTupleType(types);
    }

    virtual DSType *newTupleType(DSType *type1, DSType *type2) {
        std::vector<DSType *> types(2);
        types[0] = type1;
        types[1] = type2;
        return this->newTupleType(types);
    }

    virtual DSType *newTupleType(DSType *type1, DSType *type2, DSType *type3) {
        std::vector<DSType *> types(3);
        types[0] = type1;
        types[1] = type2;
        types[2] = type3;
        return this->newTupleType(types);
    }


    virtual DSType *newTupleType(const std::vector<DSType *> &types) {
        try {
            return this->pool->createAndGetTupleTypeIfUndefined(std::vector<DSType *>(types));
        } catch(const TypeLookupError &e) {
            return nullptr;
        }
    }



    virtual void assertDesc(const char *expected, DSType *type) {
        ASSERT_TRUE(type != nullptr);
        const char *actual = this->builder->buildDescriptor(type);
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