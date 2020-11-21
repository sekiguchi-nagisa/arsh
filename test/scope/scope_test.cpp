#include "gtest/gtest.h"

#include <scope.h>

using namespace ydsh;

struct Handle {
    unsigned int commitID;
    TYPE type;
    unsigned int index;
    FieldAttribute attr;
    unsigned short modID;
};

class ScopeTest : public ::testing::Test {
protected:
    TypePool pool;
    unsigned int gvarCount{0};
    IntrusivePtr<NameScope> builtin;
    IntrusivePtr<NameScope> top;

    ScopeTest() {
        this->builtin = IntrusivePtr<NameScope>::create(std::ref(this->gvarCount));
        this->top = IntrusivePtr<NameScope>::create(this->builtin, 1);
    }

    void expect(const Handle &e, const FieldHandle *handle) {
        ASSERT_TRUE(handle);
        ASSERT_EQ(e.commitID, handle->getCommitID());
        ASSERT_EQ(static_cast<unsigned int>(e.type), handle->getTypeID());
        ASSERT_EQ(e.index, handle->getIndex());
        ASSERT_EQ(e.attr, handle->attr());
        ASSERT_EQ(e.modID, handle->getModID());
    }

    void expect(const Handle &e, const NameLookupResult &ret) {
        ASSERT_TRUE(ret);
        this->expect(e, ret.asOk());
    }

    void expect(NameLookupError e, const NameLookupResult &ret) {
        ASSERT_FALSE(ret);
        ASSERT_EQ(e, ret.asErr());
    }
};

TEST_F(ScopeTest, builtin) {
    // base
    ASSERT_TRUE(this->builtin->isBuiltinModule());
    ASSERT_TRUE(this->builtin->isGlobal());
    ASSERT_EQ(0, this->builtin->getMaxGlobalVarIndex());
    ASSERT_FALSE(this->builtin->parent);
    ASSERT_EQ(0, this->builtin->modId);

    // define handle
    auto ret = this->builtin->defineHandle("hello", this->pool.get(TYPE::Int), FieldAttribute::ENV);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 0,
        .type = TYPE::Int,
        .index = 0,
        .attr = FieldAttribute::ENV | FieldAttribute::GLOBAL,
        .modID = 0,
    }, ret));
    ASSERT_EQ(1, this->builtin->getMaxGlobalVarIndex());
    auto *handle = ret.asOk();

    ret = this->builtin->defineHandle("hello", this->pool.get(TYPE::String), FieldAttribute::RANDOM);
    ASSERT_NO_FATAL_FAILURE(this->expect(NameLookupError::DEFINED, ret));

    // define alias
    ret = this->builtin->defineAlias("hey", *handle);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 1,
        .type = TYPE::Int,
        .index = 0,
        .attr = FieldAttribute::ENV | FieldAttribute::GLOBAL | FieldAttribute::ALIAS,
        .modID = 0,
    }, ret));

    ret = this->builtin->defineAlias("hey1", *ret.asOk());
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 2,
            .type = TYPE::Int,
            .index = 0,
            .attr = FieldAttribute::ENV | FieldAttribute::GLOBAL | FieldAttribute::ALIAS,
            .modID = 0,
    }, ret));
    ASSERT_EQ(1, this->builtin->getMaxGlobalVarIndex());

    ret = this->builtin->defineAlias("hello", *handle);
    ASSERT_NO_FATAL_FAILURE(this->expect(NameLookupError::DEFINED, ret));

    // define type alias
    ret = this->builtin->defineTypeAlias(this->pool, "hey1", this->pool.get(TYPE::Float));
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 3,
        .type = TYPE::Float,
        .index = 0,
        .attr = FieldAttribute::ALIAS ,
        .modID = 0,
    }, ret));
    ASSERT_EQ(1, this->builtin->getMaxGlobalVarIndex());

    ret = this->builtin->defineTypeAlias(this->pool, "hey1", this->pool.get(TYPE::ArithmeticError));
    ASSERT_NO_FATAL_FAILURE(this->expect(NameLookupError::DEFINED, ret));

    ret = this->builtin->defineTypeAlias(this->pool, "Int", this->pool.get(TYPE::ArithmeticError));
    ASSERT_NO_FATAL_FAILURE(this->expect(NameLookupError::DEFINED, ret));

    // lookup handle
    auto *hd = this->builtin->lookup("hello");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 0,
        .type = TYPE::Int,
        .index = 0,
        .attr = FieldAttribute::ENV | FieldAttribute::GLOBAL,
        .modID = 0,
    }, hd));

    // lookup alias
    hd = this->builtin->lookup("hey");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 1,
        .type = TYPE::Int,
        .index = 0,
        .attr = FieldAttribute::GLOBAL | FieldAttribute::ALIAS | FieldAttribute::ENV,
        .modID = 0,
    }, hd));

    hd = this->builtin->lookup("hey1");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 2,
            .type = TYPE::Int,
            .index = 0,
            .attr = FieldAttribute::GLOBAL | FieldAttribute::ALIAS | FieldAttribute::ENV,
            .modID = 0,
    }, hd));

    // lookup type alias
    hd = this->builtin->lookup(toTypeAliasFullName("hey1"));
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 3,
        .type = TYPE::Float,
        .index = 0,
        .attr = FieldAttribute::ALIAS,
        .modID = 0,
    }, hd));

    // discard
    auto point = this->builtin->getDiscardPoint();
    ret = this->builtin->defineHandle("AAA", this->pool.get(TYPE::Job), FieldAttribute{});
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 4,
            .type = TYPE::Job,
            .index = 1,
            .attr = FieldAttribute::GLOBAL,
            .modID = 0,
    }, ret));
    ASSERT_EQ(5, this->builtin->getHandles().size());
    ASSERT_EQ(2, this->builtin->getMaxGlobalVarIndex());

    this->builtin->discard(point);
    ASSERT_EQ(2, this->builtin->getMaxGlobalVarIndex());
    ASSERT_EQ(4, this->builtin->getHandles().size());
    handle = this->builtin->lookup("AAA");
    ASSERT_EQ(nullptr, handle);

    ASSERT_EQ(0, this->builtin->getCurLocalIndex());
}

TEST_F(ScopeTest, global) {
    ASSERT_TRUE(this->top->parent);
    ASSERT_EQ(0, this->top->parent->modId);
    ASSERT_EQ(1, this->top->modId);
    ASSERT_FALSE(this->top->isBuiltinModule());
    ASSERT_TRUE(this->top->isRootModule());
    ASSERT_TRUE(this->top->isGlobal());
    ASSERT_EQ(0, this->top->getMaxGlobalVarIndex());

    // define in builtin
    auto ret = this->builtin->defineHandle("AAA", this->pool.get(TYPE::Job), FieldAttribute::READ_ONLY | FieldAttribute::FUNC_HANDLE);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 0,
        .type = TYPE::Job,
        .index = 0,
        .attr = FieldAttribute::READ_ONLY | FieldAttribute::GLOBAL | FieldAttribute::FUNC_HANDLE,
        .modID = 0
    }, ret));
    ASSERT_EQ(1, this->builtin->getHandles().size());
    ASSERT_EQ(1, this->top->getMaxGlobalVarIndex());

    // define handle when defined in builtin
    ret = this->top->defineHandle("AAA", this->pool.get(TYPE::Int), FieldAttribute::SECONDS);
    ASSERT_NO_FATAL_FAILURE(this->expect(NameLookupError::DEFINED, ret));

    auto *handle = this->top->lookup("AAA");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 0,
        .type = TYPE::Job,
        .index = 0,
        .attr = FieldAttribute::READ_ONLY | FieldAttribute::GLOBAL | FieldAttribute::FUNC_HANDLE,
        .modID = 0,
    }, handle));

    // define handle
    ret = this->top->defineHandle("BBB", this->pool.get(TYPE::Signal), FieldAttribute::MOD_CONST);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 0,
        .type = TYPE::Signal,
        .index = 1,
        .attr = FieldAttribute::GLOBAL | FieldAttribute::MOD_CONST,
        .modID = 1,
    }, ret));
    handle = this->top->lookup("BBB");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Signal,
            .index = 1,
            .attr = FieldAttribute::GLOBAL | FieldAttribute::MOD_CONST,
            .modID = 1,
    }, handle));
    handle = this->builtin->lookup("BBB");
    ASSERT_EQ(nullptr, handle);
    ASSERT_EQ(0, this->top->getCurLocalIndex());
    ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
    ASSERT_EQ(2, this->top->getMaxGlobalVarIndex());
    ASSERT_EQ(2, this->builtin->getMaxGlobalVarIndex());

    // define type alias
    ret = this->top->defineTypeAlias(this->pool, "DOUBLE", this->pool.get(TYPE::Float));
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 1,
            .type = TYPE::Float,
            .index = 0,
            .attr = FieldAttribute::ALIAS,
            .modID = 1,
    }, ret));
    ret = this->top->defineTypeAlias(this->pool, "Int", this->pool.get(TYPE::String));
    ASSERT_NO_FATAL_FAILURE(this->expect(NameLookupError::DEFINED, ret));

    // define handle in builtin
    ret = this->builtin->defineHandle("CCC", this->pool.get(TYPE::StringArray), FieldAttribute{});
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 1,
            .type = TYPE::StringArray,
            .index = 2,
            .attr = FieldAttribute::GLOBAL,
            .modID = 0,
    }, ret));
    ASSERT_EQ(3, this->builtin->getMaxGlobalVarIndex());
    ASSERT_EQ(3, this->top->getMaxGlobalVarIndex());
}

TEST_F(ScopeTest, block) {  // for top level block
    // define global
    auto ret = this->top->defineHandle("AAA", this->pool.get(TYPE::Any), FieldAttribute::READ_ONLY);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Any,
            .index = 0,
            .attr = FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY,
            .modID = 1,
    }, ret));
    ASSERT_EQ(0, this->top->getMaxLocalVarIndex());

    // enter block level 0
    auto block0 = this->top->enterScope(NameScope::BLOCK);
    ASSERT_EQ(1, block0->modId);
    ASSERT_EQ(this->top, block0->parent);
    ASSERT_EQ(0, block0->getCurLocalIndex());
    ASSERT_EQ(NameScope::BLOCK, block0->kind);

    // define local
    auto *handle = block0->lookup("AAA");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Any,
            .index = 0,
            .attr = FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY,
            .modID = 1,
    }, handle));
    ret = block0->defineHandle("AAA", this->pool.get(TYPE::Error), FieldAttribute{});
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Error,
            .index = 0,
            .attr = FieldAttribute{},
            .modID = 1,
    }, ret));    // overwrite
    handle = block0->lookup("AAA");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 0,
        .type = TYPE::Error,
        .index = 0,
        .attr = FieldAttribute{},
        .modID = 1,
    }, ret));

    ASSERT_EQ(1, block0->getCurLocalIndex());
    ASSERT_EQ(1, this->top->getCurLocalIndex());

    ret = block0->defineHandle("BBB", this->pool.get(TYPE::String), FieldAttribute::ENV);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 1,
            .type = TYPE::String,
            .index = 1,
            .attr = FieldAttribute::ENV,
            .modID = 1,
    }, ret));
    ASSERT_EQ(2, block0->getCurLocalIndex());
    ASSERT_EQ(2, this->top->getCurLocalIndex());

    // define alias
    ret = block0->defineAlias("CCC", *ret.asOk());
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 2,
            .type = TYPE::String,
            .index = 1,
            .attr = FieldAttribute::ALIAS | FieldAttribute::ENV,
            .modID = 1,
    }, ret));
    ASSERT_EQ(2, block0->getCurLocalIndex());
    ASSERT_EQ(2, this->top->getCurLocalIndex());
    ASSERT_EQ(2, block0->getLocalSize());
    ASSERT_EQ(0, block0->getBaseIndex());
    ASSERT_EQ(3, block0->getHandles().size());
    ASSERT_EQ(2, block0->getMaxLocalVarIndex());

    // enter block level 1
    auto block1 = block0->enterScope(NameScope::BLOCK);
    ASSERT_EQ(block0->modId, block1->modId);
    ASSERT_EQ(block0->getMaxLocalVarIndex(), block1->getMaxLocalVarIndex());
    ASSERT_EQ(2, block1->getBaseIndex());
    ASSERT_EQ(NameScope::BLOCK, block1->kind);

    // lookup
    handle = block1->lookup("CCC");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 2,
            .type = TYPE::String,
            .index = 1,
            .attr = FieldAttribute::ALIAS | FieldAttribute::ENV,
            .modID = 1,
    }, handle));

    // define local
    ret = block1->defineHandle("CCC", this->pool.get(TYPE::KeyNotFoundError), FieldAttribute::READ_ONLY);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
        .commitID = 0,
        .type = TYPE::KeyNotFoundError,
        .index = 2,
        .attr = FieldAttribute::READ_ONLY,
        .modID = 1,
    }, ret));
    ASSERT_EQ(3, block1->getCurLocalIndex());
    ASSERT_EQ(2, block0->getCurLocalIndex());
    ASSERT_EQ(3, this->top->getCurLocalIndex());
    ASSERT_EQ(2, block0->getLocalSize());
    ASSERT_EQ(1, block1->getLocalSize());
    ASSERT_EQ(0, block0->getBaseIndex());
    ASSERT_EQ(2, block1->getBaseIndex());
    ASSERT_EQ(1, block1->getHandles().size());
    ASSERT_EQ(3, block0->getMaxLocalVarIndex());
    ASSERT_EQ(3, block1->getMaxLocalVarIndex());

    // exit block
    block1 = block1->exitScope();
    ASSERT_EQ(block0, block1);
    handle = block1->lookup("CCC");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 2,
            .type = TYPE::String,
            .index = 1,
            .attr = FieldAttribute::ALIAS | FieldAttribute::ENV,
            .modID = 1,
    }, handle));
    ASSERT_EQ(3, this->top->getCurLocalIndex());
    ASSERT_EQ(3, this->top->getMaxLocalVarIndex());
    ASSERT_EQ(2, block1->getCurLocalIndex());
    ASSERT_EQ(2, block1->getLocalSize());

    // enter block level 1
    block1 = block1->enterScope(NameScope::BLOCK);
    ASSERT_EQ(2, block1->getBaseIndex());

    ret = block1->defineHandle("DDD", this->pool.get(TYPE::UnixFD), FieldAttribute::READ_ONLY);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::UnixFD,
            .index = 2,
            .attr = FieldAttribute::READ_ONLY,
            .modID = 1,
    }, ret));
    ASSERT_EQ(3, this->top->getMaxLocalVarIndex());
    ASSERT_EQ(1, block1->getLocalSize());
    ASSERT_EQ(3, block1->getCurLocalIndex());
    ASSERT_EQ(3, block1->getMaxLocalVarIndex());

    // exit all blocks
    block1 = block1->exitScope()->exitScope();
    ASSERT_EQ(this->top, block1);
    ASSERT_EQ(3, this->top->getMaxLocalVarIndex());
    ASSERT_EQ(3, this->top->getCurLocalIndex());
    ASSERT_EQ(0, this->top->getLocalSize());

    // enter block level 0
    block0 = this->top->enterScope(NameScope::BLOCK);
    ASSERT_EQ(NameScope::BLOCK, block0->kind);
    ASSERT_EQ(0, block0->getBaseIndex());
    ASSERT_EQ(0, block0->getCurLocalIndex());
    ASSERT_EQ(3, block0->getMaxLocalVarIndex());

    ret = block0->defineHandle("AAA", this->pool.get(TYPE::Func), FieldAttribute{});
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Func,
            .index = 0,
            .attr = FieldAttribute{},
            .modID = 1,
    }, ret));
    ASSERT_EQ(0, block0->getBaseIndex());
    ASSERT_EQ(1, block0->getCurLocalIndex());
    ASSERT_EQ(3, block0->getMaxLocalVarIndex());
    ASSERT_EQ(1, block0->getLocalSize());
}

TEST_F(ScopeTest, func) {
    auto func = this->top->enterScope(NameScope::FUNC);
    ASSERT_EQ(NameScope::FUNC, func->kind);
    ASSERT_EQ(this->top, func->parent);
    ASSERT_EQ(0, func->getMaxLocalVarIndex());
    ASSERT_EQ(0, func->getBaseIndex());
    ASSERT_EQ(0, func->getCurLocalIndex());

    auto block0 = func->enterScope(NameScope::BLOCK);
    ASSERT_EQ(NameScope::BLOCK, block0->kind);
    ASSERT_EQ(func, block0->parent);
    ASSERT_EQ(0, block0->getMaxLocalVarIndex());
    ASSERT_EQ(0, block0->getBaseIndex());
    ASSERT_EQ(0, block0->getCurLocalIndex());

    // define global
    auto ret = this->top->defineHandle("GGG", this->pool.get(TYPE::Int), FieldAttribute::RANDOM | FieldAttribute::READ_ONLY);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Int,
            .index = 0,
            .attr = FieldAttribute::RANDOM | FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY,
            .modID = 1,
    }, ret));
    auto *oldHandle = ret.asOk();
    ASSERT_EQ(0, func->getMaxLocalVarIndex());
    ASSERT_EQ(0, block0->getMaxLocalVarIndex());

    auto *handle = block0->lookup("GGG");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Int,
            .index = 0,
            .attr = FieldAttribute::RANDOM | FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY,
            .modID = 1,
    }, handle));

    // define local
    ret = block0->defineHandle("GGG", this->pool.get(TYPE::Boolean), FieldAttribute::READ_ONLY);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Boolean,
            .index = 0,
            .attr = FieldAttribute::READ_ONLY,
            .modID = 1,
    }, ret));   // overwrite
    handle = block0->lookup("GGG");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 0,
            .type = TYPE::Boolean,
            .index = 0,
            .attr = FieldAttribute::READ_ONLY,
            .modID = 1,
    }, handle));
    ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
    ASSERT_EQ(1, block0->getMaxLocalVarIndex());
    ASSERT_EQ(1, func->getMaxLocalVarIndex());
    ASSERT_EQ(1, block0->getCurLocalIndex());
    ASSERT_EQ(1, block0->getLocalSize());

    ret = block0->defineHandle("QQQ", this->pool.get(TYPE::StringArray), FieldAttribute::READ_ONLY);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 1,
            .type = TYPE::StringArray,
            .index = 1,
            .attr = FieldAttribute::READ_ONLY,
            .modID = 1,
    }, ret));
    ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
    ASSERT_EQ(2, block0->getMaxLocalVarIndex());
    ASSERT_EQ(2, func->getMaxLocalVarIndex());
    ASSERT_EQ(2, block0->getCurLocalIndex());
    ASSERT_EQ(2, block0->getLocalSize());

    // define alias
    ret = block0->defineAlias("A", *oldHandle);
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 2,
            .type = TYPE::Int,
            .index = 0,
            .attr = FieldAttribute::RANDOM | FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY | FieldAttribute::ALIAS,
            .modID = 1,
    }, ret));
    handle = block0->lookup("A");
    ASSERT_NO_FATAL_FAILURE(this->expect(Handle{
            .commitID = 2,
            .type = TYPE::Int,
            .index = 0,
            .attr = FieldAttribute::RANDOM | FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY | FieldAttribute::ALIAS,
            .modID = 1,
    }, handle));
    ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
    ASSERT_EQ(2, block0->getMaxLocalVarIndex());
    ASSERT_EQ(2, func->getMaxLocalVarIndex());
    ASSERT_EQ(2, block0->getCurLocalIndex());
    ASSERT_EQ(2, block0->getLocalSize());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}