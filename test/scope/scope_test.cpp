#include "gtest/gtest.h"

#include <scope.h>

using namespace ydsh;

struct Entry {
  TYPE type;
  unsigned int index;
  HandleKind kind;
  HandleAttr attr;
  unsigned short modID;
};

class ScopeTest : public ::testing::Test {
protected:
  TypePool pool;
  unsigned int gvarCount{0};
  unsigned int modCount{0};
  NameScopePtr builtin;
  NameScopePtr top;

  ScopeTest() {
    this->builtin = NameScopePtr::create(std::ref(this->gvarCount), 0, 0);
    this->top = createGlobalScope();
  }

  NameScopePtr createGlobalScope() {
    unsigned int modIndex = this->gvarCount;
    return NameScopePtr::create(this->builtin, modIndex, ++this->modCount);
  }

  const ModType &toModType(NameScopePtr &&scope) {
    auto &type = scope->toModType(this->pool);
    this->gvarCount++;
    return type;
  }

  static void expect(const Entry &e, const HandlePtr &handle) { expect(e, handle.get()); }

  static void expect(const Entry &e, const Handle *handle) {
    ASSERT_TRUE(handle);
    ASSERT_EQ(static_cast<unsigned int>(e.type), handle->getTypeId());
    ASSERT_EQ(e.index, handle->getIndex());
    ASSERT_EQ(toString(e.kind), toString(handle->getKind()));
    ASSERT_EQ(toString(e.attr), toString(handle->attr()));
    ASSERT_EQ(e.modID, handle->getModId());
  }

  static void expect(const Entry &e, const NameRegisterResult &ret) {
    ASSERT_TRUE(ret);
    expect(e, ret.asOk());
  }

  static void expect(NameRegisterError e, const NameRegisterResult &ret) {
    ASSERT_FALSE(ret);
    ASSERT_EQ(e, ret.asErr());
  }
};

TEST_F(ScopeTest, builtin) {
  // base
  ASSERT_TRUE(this->builtin->inBuiltinModule());
  ASSERT_TRUE(this->builtin->isGlobal());
  ASSERT_EQ(0, this->builtin->getMaxGlobalVarIndex());
  ASSERT_FALSE(this->builtin->parent);
  ASSERT_EQ(0, this->builtin->modId);

  // define handle
  auto ret = this->builtin->defineHandle("hello", this->pool.get(TYPE::Int), HandleKind::ENV,
                                         HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::ENV,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      ret));
  ASSERT_EQ(1, this->builtin->getMaxGlobalVarIndex());
  auto handle = ret.asOk();

  ret = this->builtin->defineHandle("hello", this->pool.get(TYPE::String), HandleKind::ENV,
                                    HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(NameRegisterError::DEFINED, ret));

  // define alias
  ret = this->builtin->defineAlias("hey", HandlePtr(handle));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::ENV,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      ret));

  ret = this->builtin->defineAlias("hey1", HandlePtr(ret.asOk()));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::ENV,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      ret));
  ASSERT_EQ(1, this->builtin->getMaxGlobalVarIndex());

  ret = this->builtin->defineAlias("hello", HandlePtr(handle));
  ASSERT_NO_FATAL_FAILURE(this->expect(NameRegisterError::DEFINED, ret));

  // define type alias
  ret = this->builtin->defineTypeAlias(this->pool, "hey1", this->pool.get(TYPE::Float));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Float,
          .index = 0,
          .kind = HandleKind::TYPE_ALIAS,
          .attr = HandleAttr{},
          .modID = 0,
      },
      ret));
  ASSERT_EQ(1, this->builtin->getMaxGlobalVarIndex());

  ret = this->builtin->defineTypeAlias(this->pool, "hey1", this->pool.get(TYPE::ArithmeticError));
  ASSERT_NO_FATAL_FAILURE(this->expect(NameRegisterError::DEFINED, ret));

  ret = this->builtin->defineTypeAlias(this->pool, "Int", this->pool.get(TYPE::ArithmeticError));
  ASSERT_NO_FATAL_FAILURE(this->expect(NameRegisterError::DEFINED, ret));

  // lookup handle
  auto hd = this->builtin->lookup("hello");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::ENV,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      hd.asOk()));

  // lookup alias
  hd = this->builtin->lookup("hey");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::ENV,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      hd.asOk()));

  hd = this->builtin->lookup("hey1");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::ENV,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      hd.asOk()));

  // lookup type alias
  hd = this->builtin->lookup(toTypeAliasFullName("hey1"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Float,
          .index = 0,
          .kind = HandleKind::TYPE_ALIAS,
          .attr = HandleAttr{},
          .modID = 0,
      },
      hd.asOk()));

  // discard
  auto point = this->builtin->getDiscardPoint();
  ret = this->builtin->defineHandle("AAA", this->pool.get(TYPE::Job), HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Job,
          .index = 1,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      ret));
  ASSERT_EQ(5, this->builtin->getHandles().size());
  ASSERT_EQ(2, this->builtin->getMaxGlobalVarIndex());

  this->builtin->discard(point);
  ASSERT_EQ(2, this->builtin->getMaxGlobalVarIndex());
  ASSERT_EQ(4, this->builtin->getHandles().size());
  hd = this->builtin->lookup("AAA");
  ASSERT_FALSE(hd);

  ASSERT_EQ(0, this->builtin->getCurLocalIndex());
}

TEST_F(ScopeTest, global) {
  ASSERT_TRUE(this->top->parent);
  ASSERT_EQ(0, this->top->parent->modId);
  ASSERT_EQ(1, this->top->modId);
  ASSERT_FALSE(this->top->inBuiltinModule());
  ASSERT_TRUE(this->top->inRootModule());
  ASSERT_TRUE(this->top->isGlobal());
  ASSERT_EQ(0, this->top->getMaxGlobalVarIndex());

  // define in builtin
  auto ret = this->builtin->defineHandle("AAA", this->pool.get(TYPE::Job), HandleAttr::READ_ONLY);
  ASSERT_NO_FATAL_FAILURE(this->expect(Entry{.type = TYPE::Job,
                                             .index = 0,
                                             .kind = HandleKind::VAR,
                                             .attr = HandleAttr::READ_ONLY | HandleAttr::GLOBAL,
                                             .modID = 0},
                                       ret));
  ASSERT_EQ(1, this->builtin->getHandles().size());
  ASSERT_EQ(1, this->top->getMaxGlobalVarIndex());

  // define handle when defined in builtin
  ret = this->top->defineHandle("AAA", this->pool.get(TYPE::Int), HandleKind::ENV, HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(NameRegisterError::DEFINED, ret));

  auto handle = this->top->lookup("AAA");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Job,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY | HandleAttr::GLOBAL,
          .modID = 0,
      },
      handle.asOk()));

  // define handle
  ret = this->top->defineHandle("BBB", this->pool.get(TYPE::Signal), HandleKind::MOD_CONST,
                                HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Signal,
          .index = 1,
          .kind = HandleKind::MOD_CONST,
          .attr = HandleAttr::GLOBAL,
          .modID = 1,
      },
      ret));
  handle = this->top->lookup("BBB");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Signal,
          .index = 1,
          .kind = HandleKind::MOD_CONST,
          .attr = HandleAttr::GLOBAL,
          .modID = 1,
      },
      handle.asOk()));
  handle = this->builtin->lookup("BBB");
  ASSERT_FALSE(handle);
  ASSERT_EQ(0, this->top->getCurLocalIndex());
  ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
  ASSERT_EQ(2, this->top->getMaxGlobalVarIndex());
  ASSERT_EQ(2, this->builtin->getMaxGlobalVarIndex());

  // define type alias
  ret = this->top->defineTypeAlias(this->pool, "DOUBLE", this->pool.get(TYPE::Float));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Float,
          .index = 0,
          .kind = HandleKind::TYPE_ALIAS,
          .attr = HandleAttr{},
          .modID = 1,
      },
      ret));
  ret = this->top->defineTypeAlias(this->pool, "Int", this->pool.get(TYPE::String));
  ASSERT_NO_FATAL_FAILURE(this->expect(NameRegisterError::DEFINED, ret));

  // define handle in builtin
  ret = this->builtin->defineHandle("CCC", this->pool.get(TYPE::StringArray), HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::StringArray,
          .index = 2,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL,
          .modID = 0,
      },
      ret));
  ASSERT_EQ(3, this->builtin->getMaxGlobalVarIndex());
  ASSERT_EQ(3, this->top->getMaxGlobalVarIndex());
}

TEST_F(ScopeTest, block) { // for top level block
  // define global
  auto ret = this->top->defineHandle("AAA", this->pool.get(TYPE::Any), HandleAttr::READ_ONLY);
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Any,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 1,
      },
      ret));
  ASSERT_EQ(0, this->top->getMaxLocalVarIndex());

  // enter block level 0
  auto block0 = this->top->enterScope(NameScope::BLOCK);
  ASSERT_EQ(1, block0->modId);
  ASSERT_EQ(this->top, block0->parent);
  ASSERT_EQ(0, block0->getCurLocalIndex());
  ASSERT_EQ(NameScope::BLOCK, block0->kind);

  // define local
  auto handle = block0->lookup("AAA");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Any,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 1,
      },
      handle.asOk()));
  ret = block0->defineHandle("AAA", this->pool.get(TYPE::Error), HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Error,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr{},
          .modID = 1,
      },
      ret)); // overwrite
  handle = block0->lookup("AAA");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Error,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr{},
          .modID = 1,
      },
      ret));

  ASSERT_EQ(1, block0->getCurLocalIndex());
  ASSERT_EQ(1, this->top->getCurLocalIndex());

  ret = block0->defineHandle("BBB", this->pool.get(TYPE::String), HandleKind::ENV, HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::String,
          .index = 1,
          .kind = HandleKind::ENV,
          .attr = HandleAttr{},
          .modID = 1,
      },
      ret));
  ASSERT_EQ(2, block0->getCurLocalIndex());
  ASSERT_EQ(2, this->top->getCurLocalIndex());

  // define alias
  ret = block0->defineAlias("CCC", HandlePtr(ret.asOk()));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::String,
          .index = 1,
          .kind = HandleKind::ENV,
          .attr = HandleAttr{},
          .modID = 1,
      },
      ret));
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
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::String,
          .index = 1,
          .kind = HandleKind::ENV,
          .attr = HandleAttr{},
          .modID = 1,
      },
      handle.asOk()));

  // define local
  ret = block1->defineHandle("CCC", this->pool.get(TYPE::KeyNotFoundError), HandleAttr::READ_ONLY);
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::KeyNotFoundError,
          .index = 2,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY,
          .modID = 1,
      },
      ret));
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
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::String,
          .index = 1,
          .kind = HandleKind::ENV,
          .attr = HandleAttr{},
          .modID = 1,
      },
      handle.asOk()));
  ASSERT_EQ(3, this->top->getCurLocalIndex());
  ASSERT_EQ(3, this->top->getMaxLocalVarIndex());
  ASSERT_EQ(2, block1->getCurLocalIndex());
  ASSERT_EQ(2, block1->getLocalSize());

  // enter block level 1
  block1 = block1->enterScope(NameScope::BLOCK);
  ASSERT_EQ(2, block1->getBaseIndex());

  ret = block1->defineHandle("DDD", this->pool.get(TYPE::UnixFD), HandleAttr::READ_ONLY);
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::UnixFD,
          .index = 2,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY,
          .modID = 1,
      },
      ret));
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

  ret = block0->defineHandle("AAA", this->pool.get(TYPE::Func), HandleAttr{});
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Func,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr{},
          .modID = 1,
      },
      ret));
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
  auto ret = this->top->defineHandle("GGG", this->pool.get(TYPE::Int), HandleAttr::READ_ONLY);
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 1,
      },
      ret));
  auto oldHandle = ret.asOk();
  ASSERT_EQ(0, func->getMaxLocalVarIndex());
  ASSERT_EQ(0, block0->getMaxLocalVarIndex());

  auto handle = block0->lookup("GGG");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 1,
      },
      handle.asOk()));

  // define local
  ret = block0->defineHandle("GGG", this->pool.get(TYPE::Boolean), HandleAttr::READ_ONLY);
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Boolean,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY,
          .modID = 1,
      },
      ret)); // overwrite
  handle = block0->lookup("GGG");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Boolean,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY,
          .modID = 1,
      },
      handle.asOk()));
  ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
  ASSERT_EQ(1, block0->getMaxLocalVarIndex());
  ASSERT_EQ(1, func->getMaxLocalVarIndex());
  ASSERT_EQ(1, block0->getCurLocalIndex());
  ASSERT_EQ(1, block0->getLocalSize());

  ret = block0->defineHandle("QQQ", this->pool.get(TYPE::StringArray), HandleAttr::READ_ONLY);
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::StringArray,
          .index = 1,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY,
          .modID = 1,
      },
      ret));
  ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
  ASSERT_EQ(2, block0->getMaxLocalVarIndex());
  ASSERT_EQ(2, func->getMaxLocalVarIndex());
  ASSERT_EQ(2, block0->getCurLocalIndex());
  ASSERT_EQ(2, block0->getLocalSize());

  // define alias
  ret = block0->defineAlias("A", HandlePtr(oldHandle));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 1,
      },
      ret));
  handle = block0->lookup("A");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 1,
      },
      handle.asOk()));
  ASSERT_EQ(0, this->top->getMaxLocalVarIndex());
  ASSERT_EQ(2, block0->getMaxLocalVarIndex());
  ASSERT_EQ(2, func->getMaxLocalVarIndex());
  ASSERT_EQ(2, block0->getCurLocalIndex());
  ASSERT_EQ(2, block0->getLocalSize());
}

TEST_F(ScopeTest, import1) {
  // define mod type
  auto mod = this->createGlobalScope();
  mod->defineHandle("AAA", this->pool.get(TYPE::Int), HandleAttr::READ_ONLY);
  mod->defineHandle("_AAA", this->pool.get(TYPE::String), HandleKind::ENV, HandleAttr{});
  mod->defineTypeAlias(this->pool, "_string", this->pool.get(TYPE::String));
  mod->defineTypeAlias(this->pool, "integer", this->pool.get(TYPE::Int));
  auto &modType = this->toModType(std::move(mod));

  // named import
  auto s = this->top->importForeignHandles(this->pool, modType, ImportedModKind{});
  ASSERT_EQ("", s);
  ASSERT_EQ(3, this->top->getMaxGlobalVarIndex());
  ASSERT_EQ(1, this->top->getHandles().size());

  s = this->top->importForeignHandles(this->pool, modType, ImportedModKind{});
  ASSERT_EQ("", s);
  ASSERT_EQ(3, this->top->getMaxGlobalVarIndex());
  ASSERT_EQ(1, this->top->getHandles().size());

  auto handle = this->top->lookup("AAA");
  ASSERT_FALSE(handle);
  handle = this->top->lookup("_AAA");
  ASSERT_FALSE(handle);
  handle = this->top->lookup(toTypeAliasFullName("_string"));
  ASSERT_FALSE(handle);
  handle = this->top->lookup(toTypeAliasFullName("integer"));
  ASSERT_FALSE(handle);

  // global import
  s = this->top->importForeignHandles(this->pool, modType, ImportedModKind::GLOBAL);
  ASSERT_EQ("", s);
  ASSERT_EQ(3, this->top->getMaxGlobalVarIndex());
  ASSERT_EQ(4, this->top->getHandles().size());

  s = this->top->importForeignHandles(this->pool, modType, ImportedModKind::GLOBAL);
  ASSERT_EQ("", s);
  ASSERT_EQ(3, this->top->getMaxGlobalVarIndex());
  ASSERT_EQ(4, this->top->getHandles().size());

  handle = this->top->lookup("AAA");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 2,
      },
      handle.asOk()));

  handle = this->top->lookup("_AAA"); // not import private symbol
  ASSERT_FALSE(handle);

  handle = this->top->lookup(toTypeAliasFullName("_string")); // not import private symbol
  ASSERT_FALSE(handle);

  handle = this->top->lookup(toTypeAliasFullName("integer"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::TYPE_ALIAS,
          .attr = HandleAttr{},
          .modID = 2,
      },
      handle.asOk()));

  // ModType
  auto mod2 = this->createGlobalScope();
  mod2->defineHandle("GGG", this->pool.get(TYPE::Job), HandleAttr{});
  auto &modType2 = this->toModType(std::move(mod2));

  this->top->importForeignHandles(this->pool, modType2, ImportedModKind{});

  auto &modType3 = this->toModType(std::move(this->top));
  ASSERT_EQ(2, modType3.getChildSize());
  ASSERT_TRUE(modType3.getChildAt(0).isGlobal());
  ASSERT_FALSE(modType3.getChildAt(1).isGlobal());

  // lookup from ModType
  auto *handle2 = modType3.lookupVisibleSymbolAtModule(this->pool, "GGG");
  ASSERT_FALSE(handle2);

  handle2 = modType3.lookupVisibleSymbolAtModule(this->pool, "AAA");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 2,
      },
      handle2));

  handle2 = modType3.lookupVisibleSymbolAtModule(this->pool, "_AAA"); // not import private symbol
  ASSERT_FALSE(handle2);

  handle2 = modType3.lookupVisibleSymbolAtModule(
      this->pool, toTypeAliasFullName("_string")); // not import private symbol
  ASSERT_FALSE(handle2);

  handle2 = modType3.lookupVisibleSymbolAtModule(this->pool, toTypeAliasFullName("integer"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::TYPE_ALIAS,
          .attr = HandleAttr{},
          .modID = 2,
      },
      handle2));

  ASSERT_EQ(0, modType3.getHandleMap().size());
  ASSERT_EQ(modType, this->pool.get(modType3.getChildAt(0).typeId()));
  ASSERT_EQ(modType2, this->pool.get(modType3.getChildAt(1).typeId()));
}

TEST_F(ScopeTest, import2) {
  // define mod type
  auto mod = this->createGlobalScope();
  mod->defineHandle("AAA", this->pool.get(TYPE::Int), HandleAttr::READ_ONLY);
  mod->defineHandle("_AAA", this->pool.get(TYPE::String), HandleKind::ENV, HandleAttr{});
  mod->defineTypeAlias(this->pool, "_string", this->pool.get(TYPE::String));
  mod->defineTypeAlias(this->pool, "integer", this->pool.get(TYPE::Int));
  auto &modType = this->toModType(std::move(mod));
  ASSERT_EQ(2, modType.getModId());
  ASSERT_EQ(0, modType.getIndex());

  // inlined import
  auto mod2 = this->createGlobalScope();
  ASSERT_EQ(3, mod2->modId);
  mod2->defineHandle("BBB", this->pool.get(TYPE::Float), HandleAttr::READ_ONLY);
  mod2->defineTypeAlias(this->pool, "float", this->pool.get(TYPE::Float));
  auto s = mod2->importForeignHandles(this->pool, modType,
                                      ImportedModKind::GLOBAL | ImportedModKind::INLINED);

  ASSERT_EQ("", s);
  ASSERT_EQ(4, mod2->getMaxGlobalVarIndex());
  ASSERT_EQ(5, mod2->getHandles().size());

  auto handle = mod2->lookup(toModHolderName(modType.getModId(), true));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = static_cast<TYPE>(modType.typeId()),
          .index = 0, // toModType api not increment
          .kind = HandleKind::INLINED_MOD,
          .attr = HandleAttr::READ_ONLY | HandleAttr::GLOBAL,
          .modID = 3,
      },
      handle.asOk()));

  // ModType
  auto &modType2 = this->toModType(std::move(mod2));
  ASSERT_EQ(3, modType2.getModId());
  auto hd = modType2.lookup(this->pool, "AAA");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY | HandleAttr::GLOBAL,
          .modID = 2,
      },
      hd));
  ASSERT_EQ(hd.get(), modType2.lookupVisibleSymbolAtModule(this->pool, "AAA"));

  hd = modType2.lookup(this->pool, toTypeAliasFullName("integer"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Int,
          .index = 0,
          .kind = HandleKind::TYPE_ALIAS,
          .attr = HandleAttr{},
          .modID = 2,
      },
      hd));
  ASSERT_EQ(hd.get(),
            modType2.lookupVisibleSymbolAtModule(this->pool, toTypeAliasFullName("integer")));

  hd = modType2.lookup(this->pool, toTypeAliasFullName("_string"));
  ASSERT_FALSE(hd);
  hd = modType2.lookup(this->pool, "_AAA");
  ASSERT_FALSE(hd);

  hd = modType2.lookup(this->pool, "BBB");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Float,
          .index = 3,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::READ_ONLY | HandleAttr::GLOBAL,
          .modID = 3,
      },
      hd));

  hd = modType2.lookup(this->pool, toTypeAliasFullName("float"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Float,
          .index = 0,
          .kind = HandleKind::TYPE_ALIAS,
          .attr = HandleAttr{},
          .modID = 3,
      },
      hd));

  // nested import
  s = this->top->importForeignHandles(this->pool, modType2, ImportedModKind::GLOBAL);
  ASSERT_EQ("", s);
  handle = this->top->lookup(toModHolderName(modType2.getModId(), true));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = static_cast<TYPE>(modType2.typeId()),
          .index = 3,
          .kind = HandleKind::GLOBAL_MOD,
          .attr = HandleAttr::GLOBAL | HandleAttr::READ_ONLY,
          .modID = 1,
      },
      handle.asOk()));

  handle = this->top->lookup("AAA");
  ASSERT_TRUE(handle.asOk()->has(HandleAttr::READ_ONLY | HandleAttr::GLOBAL));
  ASSERT_EQ(2, handle.asOk()->getModId());
  ASSERT_EQ(0, handle.asOk()->getIndex());

  handle = this->top->lookup(toTypeAliasFullName("integer"));
  ASSERT_EQ(toString(HandleKind::TYPE_ALIAS), toString(handle.asOk()->getKind()));
  ASSERT_EQ(2, handle.asOk()->getModId());
  ASSERT_EQ(0, handle.asOk()->getIndex());

  handle = this->top->lookup(toTypeAliasFullName("_string"));
  ASSERT_FALSE(handle);
  handle = this->top->lookup("_AAA");
  ASSERT_FALSE(handle);

  handle = this->top->lookup("BBB");
  ASSERT_TRUE(handle.asOk()->has(HandleAttr::READ_ONLY | HandleAttr::GLOBAL));
  ASSERT_EQ(3, handle.asOk()->getModId());
  ASSERT_EQ(3, handle.asOk()->getIndex());

  handle = this->top->lookup(toTypeAliasFullName("float"));
  ASSERT_EQ(toString(HandleKind::TYPE_ALIAS), toString(handle.asOk()->getKind()));
  ASSERT_EQ(3, handle.asOk()->getModId());
  ASSERT_EQ(0, handle.asOk()->getIndex());
}

TEST_F(ScopeTest, conflict) {
  // define mod type
  auto mod = this->createGlobalScope();
  mod->defineHandle("AAA", this->pool.get(TYPE::Int), HandleAttr::READ_ONLY);
  mod->defineHandle("_AAA", this->pool.get(TYPE::String), HandleKind::ENV, HandleAttr{});
  mod->defineTypeAlias(this->pool, "_string", this->pool.get(TYPE::String));
  mod->defineTypeAlias(this->pool, "integer", this->pool.get(TYPE::Int));
  auto &modType = this->toModType(std::move(mod));

  //
  auto point = this->top->getDiscardPoint();

  this->top->defineHandle("AAA", this->pool.get(TYPE::Regex), HandleAttr{});
  auto ret = this->top->importForeignHandles(this->pool, modType, ImportedModKind::GLOBAL);
  ASSERT_EQ("AAA", ret);

  auto handle = this->top->lookup("AAA");
  ASSERT_NO_FATAL_FAILURE(this->expect(
      Entry{
          .type = TYPE::Regex,
          .index = 3,
          .kind = HandleKind::VAR,
          .attr = HandleAttr::GLOBAL,
          .modID = 1,
      },
      handle.asOk()));
  handle = this->top->lookup(toTypeAliasFullName("integer"));
  ASSERT_FALSE(handle);

  this->top->discard(point);
  handle = this->top->lookup("AAA");
  ASSERT_FALSE(handle);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}