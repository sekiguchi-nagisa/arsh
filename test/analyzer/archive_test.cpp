
#include "gtest/gtest.h"

#include "../arg_parser_helper.hpp"
#include "index_test.hpp"

#include "analyzer.h"
#include "archive.h"

using namespace arsh::lsp;
using namespace arsh;

TEST(SourceTest, base) {
  SourceManager srcMan;
  auto src = srcMan.findById(ModId{111});
  ASSERT_FALSE(src);
  src = srcMan.find("ssss");
  ASSERT_FALSE(src);

  src = srcMan.update("/dummy1", 10, "hello");
  ASSERT_TRUE(src);
  ASSERT_EQ(10, src->getVersion());
  ASSERT_EQ("/dummy1", src->getPath());
  ASSERT_EQ("hello\n", src->getContent());
  ASSERT_EQ(1, toUnderlying(src->getSrcId()));

  src = srcMan.update("/dummy1", 12, "world");
  ASSERT_TRUE(src);
  ASSERT_EQ(12, src->getVersion());
  ASSERT_EQ("/dummy1", src->getPath());
  ASSERT_EQ("world\n", src->getContent());
  ASSERT_EQ(1, toUnderlying(src->getSrcId()));

  src = srcMan.update("/dummy2", 1, "");
  ASSERT_TRUE(src);
  ASSERT_EQ(1, src->getVersion());
  ASSERT_EQ("/dummy2", src->getPath());
  ASSERT_EQ("\n", src->getContent());
  ASSERT_EQ(2, toUnderlying(src->getSrcId()));
}

TEST(SourceTest, remove) {
  SourceManager srcMan;
  ASSERT_FALSE(srcMan.remove(ModId{0}));
  ASSERT_FALSE(srcMan.remove(ModId{1}));

  auto src = srcMan.update("/dummy1", 10, "hello11");
  ASSERT_TRUE(src);
  ASSERT_EQ(ModId{1}, src->getSrcId());

  src = srcMan.update("/dummy2", 10, "hello22");
  ASSERT_TRUE(src);
  ASSERT_EQ(ModId{2}, src->getSrcId());

  src = srcMan.update("/dummy3", 10, "hello33");
  ASSERT_TRUE(src);
  ASSERT_EQ(ModId{3}, src->getSrcId());

  // remove and re-assign
  auto removed = srcMan.remove(ModId{1});
  ASSERT_TRUE(removed);
  ASSERT_EQ("/dummy1", removed->getPath());
  ASSERT_EQ(ModId{1}, removed->getSrcId());
  src = srcMan.findById(ModId{1});
  ASSERT_FALSE(src);

  src = srcMan.update("/dummy4", 10, "hello44");
  ASSERT_TRUE(src);
  ASSERT_EQ(ModId{1}, src->getSrcId());
  src = srcMan.findById(ModId{1});
  ASSERT_TRUE(src);
  ASSERT_EQ("hello44\n", src->getContent());

  // remove last and re-assign
  removed = srcMan.remove(ModId{3});
  ASSERT_TRUE(removed);
  ASSERT_EQ("/dummy3", removed->getPath());
  ASSERT_EQ(ModId{3}, removed->getSrcId());
  src = srcMan.findById(ModId{3});
  ASSERT_FALSE(src);

  src = srcMan.update("/dummy5", 10, "hello55");
  ASSERT_TRUE(src);
  ASSERT_EQ(ModId{3}, src->getSrcId());
  src = srcMan.findById(ModId{3});
  ASSERT_TRUE(src);
  ASSERT_EQ("hello55\n", src->getContent());
}

struct ArchiveBuilder {
  ModId id;
  std::vector<ArchiveBuilder> children;

  ModuleArchivePtr build(ModuleArchives &archives) const {
    std::vector<Archive> handles;
    std::vector<ModuleArchive::Imported> imported;
    imported.reserve(this->children.size());
    for (auto &e : this->children) {
      auto child = e.build(archives);
      imported.emplace_back(ModuleArchive::Imported::create(ImportedModKind::GLOBAL,
                                                            child->getModId(), child->getHash()));
    }
    auto ret = std::make_shared<ModuleArchive>(this->id, ModAttr{}, std::move(handles),
                                               std::move(imported));
    archives.add(ret);
    return ret;
  }
};

template <size_t N, typename... T>
ArchiveBuilder tree(T &&...args) {
  return ArchiveBuilder{
      .id = ModId{N},
      .children = {std::forward<T>(args)...},
  };
}

TEST(DependentTest, deps1) {
  ModuleArchives archives;
  auto t = tree<1>(tree<2>(tree<5>(tree<7>()), tree<6>()), tree<3>(tree<4>()));
  auto archive = t.build(archives);
  ASSERT_EQ(2, archive->getImported().size());
  ASSERT_EQ(2, toUnderlying(archive->getImported()[0].modId));
  ASSERT_EQ(3, toUnderlying(archive->getImported()[1].modId));
  auto deps = archive->getDepsByTopologicalOrder(archives);
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(7, toUnderlying(deps[0]->getModId()));
  ASSERT_EQ(5, toUnderlying(deps[1]->getModId()));
  ASSERT_EQ(6, toUnderlying(deps[2]->getModId()));
  ASSERT_EQ(2, toUnderlying(deps[3]->getModId()));
  ASSERT_EQ(4, toUnderlying(deps[4]->getModId()));
  ASSERT_EQ(3, toUnderlying(deps[5]->getModId()));
}

TEST(DependentTest, deps2) {
  ModuleArchives archives;
  auto t1 = tree<1>(tree<2>(), tree<3>(tree<4>()));
  auto t2 = tree<5>(tree<2>(), t1, tree<6>(tree<4>()), tree<3>(tree<4>()));
  auto t3 = tree<7>(t2, t1);
  auto archive = t3.build(archives);
  ASSERT_EQ(2, archive->getImported().size());
  ASSERT_EQ(5, toUnderlying(archive->getImported()[0].modId));
  ASSERT_EQ(1, toUnderlying(archive->getImported()[1].modId));
  auto deps = archive->getDepsByTopologicalOrder(archives);
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(2, toUnderlying(deps[0]->getModId()));
  ASSERT_EQ(4, toUnderlying(deps[1]->getModId()));
  ASSERT_EQ(3, toUnderlying(deps[2]->getModId()));
  ASSERT_EQ(1, toUnderlying(deps[3]->getModId()));
  ASSERT_EQ(6, toUnderlying(deps[4]->getModId()));
  ASSERT_EQ(5, toUnderlying(deps[5]->getModId()));
}

TEST(DependentTest, deps3) {
  ModuleArchives archives;
  auto t = tree<1>();
  auto archive = t.build(archives);
  ASSERT_EQ(0, archive->getImported().size());
  auto deps = archive->getDepsByTopologicalOrder(archives);
  ASSERT_EQ(0, deps.size());
}

class ArchiveTest : public ::testing::Test {
private:
  SysConfig sysConfig;
  SourceManager srcMan;
  ModuleArchives archives;
  AnalyzerContextPtr orgCtx;
  AnalyzerContextPtr newCtx;

protected:
  const unsigned int builtinIdOffset;

  static AnalyzerContextPtr newctx(const SysConfig &config, SourceManager &srcMan,
                                   ModuleArchives &archives) {
    static unsigned int count = 0;
    std::string path = "/dummy_";
    path += std::to_string(count++);
    auto src = srcMan.update(path, 0, "");
    archives.reserve(src->getSrcId());
    return std::make_unique<AnalyzerContext>(config, src);
  }

  AnalyzerContextPtr newctx() { return newctx(this->sysConfig, this->srcMan, this->archives); }

  static auto toSorted(const std::unordered_map<std::string, HandlePtr> &handleMap) {
    using Entry = std::pair<std::string, HandlePtr>;
    std::vector<Entry> ret;
    ret.reserve(handleMap.size());
    for (auto &e : handleMap) {
      ret.emplace_back(e.first, e.second);
    }
    std::sort(ret.begin(), ret.end(),
              [](const Entry &x, const Entry &y) { return x.first < y.first; });
    return ret;
  }

public:
  ArchiveTest()
      : orgCtx(newctx(this->sysConfig, this->srcMan, this->archives)),
        newCtx(newctx(this->sysConfig, this->srcMan, this->archives)),
        builtinIdOffset(this->orgCtx->getPool().getDiscardPoint().typeIdOffset) {}

  TypePool &pool() { return this->orgCtx->getPool(); }

  NameScope &scope() const { return *this->orgCtx->getScope(); }

  const TypePool &newPool() const { return this->newCtx->getPool(); }

  void defineAndArchive(const char *fieldName, const char *typeName,
                        HandleKind kind = HandleKind::VAR, HandleAttr attr = {}) {
    ASSERT_TRUE(typeName);
    auto ret = this->orgCtx->getPool().getType(typeName);
    ASSERT_TRUE(ret);
    this->defineAndArchive(fieldName, *ret, kind, attr);
  }

  void defineAndArchive(const char *fieldName, const Type &orgType,
                        HandleKind kind = HandleKind::VAR, HandleAttr attr = {}) {
    ASSERT_TRUE(fieldName);
    ASSERT_TRUE(orgType.typeId() < this->pool().getDiscardPoint().typeIdOffset);
    ASSERT_EQ(orgType, this->orgCtx->getPool().get(orgType.typeId()));

    // define handle
    auto ret1 = this->orgCtx->getScope()->defineHandle(fieldName, orgType, kind, attr);
    ASSERT_TRUE(ret1);
    auto &orgHandle = *ret1.asOk();

    // serialize
    Archiver archiver(this->orgCtx->getPool(), this->builtinIdOffset);
    auto archive = archiver.pack(fieldName, orgHandle);
    ASSERT_FALSE(archive.getData().empty());

    // deserialize
    auto ret2 = archive.unpack(this->newCtx->getPool()); // deserialize in another context
    ASSERT_TRUE(ret2.second);
    auto &newHandle = *ret2.second;

    // compare 2 handles
    ASSERT_EQ(orgHandle.getModId(), newHandle.getModId());
    ASSERT_EQ(orgHandle.getIndex(), newHandle.getIndex());
    ASSERT_EQ(toString(orgHandle.attr()), toString(newHandle.attr()));
    ASSERT_TRUE(newHandle.getTypeId() <= this->newPool().getDiscardPoint().typeIdOffset);
    auto &newType = this->newPool().get(newHandle.getTypeId());
    ASSERT_EQ(orgType.getNameRef(), newType.getNameRef());
  }

  void define(const char *fieldName, const Type &orgType, HandleKind kind = HandleKind::VAR,
              HandleAttr attr = {}) {
    define(*this->orgCtx, fieldName, orgType, kind, attr);
  }

  static void define(AnalyzerContext &ctx, const char *fieldName, const Type &type,
                     HandleKind kind = HandleKind::VAR, HandleAttr attr = {}) {
    ASSERT_TRUE(fieldName);
    ASSERT_TRUE(type.typeId() < ctx.getPool().getDiscardPoint().typeIdOffset);
    ASSERT_EQ(type, ctx.getPool().get(type.typeId()));

    auto ret = ctx.getScope()->defineHandle(fieldName, type, kind, attr);
    ASSERT_TRUE(ret);
    auto handle = ret.asOk();
    ASSERT_EQ(type.typeId(), handle->getTypeId());
    auto orgAttr = attr;
    unsetFlag(orgAttr, HandleAttr::GLOBAL);
    auto newAttr = handle->attr();
    unsetFlag(newAttr, HandleAttr::GLOBAL);
    ASSERT_EQ(toString(orgAttr), toString(newAttr));
  }

  static PackedParamNames packedParamNames(std::vector<std::string> &&names) {
    PackedParamNamesBuilder builder(16);
    for (auto &e : names) {
      builder.addParamName(e);
    }
    return std::move(builder).build();
  }

  template <typename Func>
  const ModType &loadMod(bool global, Func func) {
    return this->loadModAt(*this->orgCtx, global, std::move(func));
  }

  template <typename Func>
  const ModType &loadModAt(AnalyzerContext &parent, bool global, Func func) {
    auto ctx = newctx(this->sysConfig, this->srcMan, this->archives);
    func(*ctx);
    auto archive = std::move(*ctx).buildArchive(this->archives);
    auto *type = loadFromArchive(this->archives, parent.getPool(), *archive);
    assert(type);
    auto importOp = ImportedModKind{};
    if (global) {
      setFlag(importOp, ImportedModKind::GLOBAL);
    }
    parent.getScope()->importForeignHandles(parent.getPool(), *type, importOp);
    return *type;
  }

  void archiveMod(std::vector<std::string> &&expected = {}) {
    // serialize
    auto poolPtr = this->orgCtx->getPoolPtr();
    auto archive = std::move(*this->orgCtx).buildArchive(this->archives);
    ASSERT_TRUE(archive);
    auto id = archive->getModId();
    auto *orgModType = poolPtr->getModTypeById(id);
    ASSERT_TRUE(orgModType);

    // deserialize
    auto *newModType = loadFromArchive(this->archives, this->newCtx->getPool(), *archive);
    ASSERT_TRUE(newModType);

    // compare
    ASSERT_EQ(orgModType->getModId(), newModType->getModId());
    ASSERT_EQ(orgModType->getChildSize(), newModType->getChildSize());
    for (unsigned int i = 0; i < orgModType->getChildSize(); i++) {
      auto oe = orgModType->getChildAt(i);
      auto ne = newModType->getChildAt(i);
      ASSERT_EQ(oe.isGlobal(), ne.isGlobal());
      auto &om = poolPtr->get(oe.typeId());
      ASSERT_TRUE(om.isModType());
      auto &nm = this->newPool().get(ne.typeId());
      ASSERT_TRUE(nm.isModType());
      ASSERT_EQ(cast<ModType>(om).getModId(), cast<ModType>(nm).getModId());
      ASSERT_EQ(om.getNameRef(), nm.getNameRef());
    }

    ASSERT_EQ(orgModType->getHandleMap().size(), newModType->getHandleMap().size());
    ASSERT_EQ(poolPtr->getDiscardPoint().typeIdOffset,
              this->newPool().getDiscardPoint().typeIdOffset);

    auto orgHandles = toSorted(orgModType->getHandleMap());
    auto newHandles = toSorted(newModType->getHandleMap());
    ASSERT_EQ(orgHandles.size(), newHandles.size());
    for (unsigned int i = 0; i < orgHandles.size(); i++) {
      auto &orgEntry = orgHandles[i];
      auto &newEntry = newHandles[i];
      ASSERT_EQ(orgEntry.first, newEntry.first);
      ASSERT_EQ(orgEntry.second->getModId(), newEntry.second->getModId());
      ASSERT_EQ(toString(orgEntry.second->attr()), toString(newEntry.second->attr()));
    }

    // check specified names
    for (auto &e : expected) {
      auto handle = newModType->lookup(this->newPool(), e);
      ASSERT_TRUE(handle);
    }
  }
};

TEST_F(ArchiveTest, base) {
  auto ctx = this->newctx();
  auto handle = ctx->getScope()->find("COMP_HOOK");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(handle->has(HandleAttr::GLOBAL));
  ASSERT_EQ(0, toUnderlying(handle->getModId()));
  handle = ctx->getScope()->find("TRUE");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(handle->has(HandleAttr::GLOBAL | HandleAttr::READ_ONLY));
  ASSERT_EQ(0, toUnderlying(handle->getModId()));
  ASSERT_TRUE(ctx->getTypeIdOffset() <= UINT8_MAX);
}

TEST_F(ArchiveTest, predefined) {
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("a", "[String]"));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("b", "String", HandleKind::ENV));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("c", "((Module, [String]) -> Bool)?"));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("d", "(Signal) -> Void"));
}

TEST_F(ArchiveTest, array) {
  //
  auto ret1 = this->pool().createArrayType(this->pool().get(TYPE::GlobError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa", type1));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("bbb", type1, HandleKind::TYPE_ALIAS));

  //
  ret1 = this->pool().createArrayType(type1);
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_TRUE(type2.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", "[[GlobError]]"));
}

TEST_F(ArchiveTest, map) {
  //
  auto ret1 =
      this->pool().createMapType(this->pool().get(TYPE::Signal), this->pool().get(TYPE::GlobError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("aaa", type1, HandleKind::VAR, HandleAttr::READ_ONLY));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("bbb", type1, HandleKind::TYPE_ALIAS));

  //
  ret1 = this->pool().createMapType(this->pool().get(TYPE::String), this->pool().get(TYPE::Bool));
  ASSERT_TRUE(ret1);
  ret1 = this->pool().createMapType(this->pool().get(TYPE::Float), *ret1.asOk());
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_TRUE(type2.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", type2));
}

TEST_F(ArchiveTest, tuple) {
  //
  std::vector<const Type *> types;
  types.push_back(&this->pool().get(TYPE::IllegalAccessError));
  types.push_back(&this->pool().get(TYPE::TypeCastError));
  types.push_back(&this->pool().get(TYPE::StringArray));
  auto ret1 = this->pool().createTupleType(std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("aaa12", type1, HandleKind::VAR, HandleAttr::READ_ONLY));

  //
  ret1 = this->pool().createMapType(this->pool().get(TYPE::Int), this->pool().get(TYPE::Int));
  ASSERT_TRUE(ret1);
  types = std::vector<const Type *>();
  types.push_back(ret1.asOk());
  ret1 = this->pool().createTupleType(std::move(types));
  ASSERT_TRUE(ret1);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("bbb", "([Int : Int],)"));
}

TEST_F(ArchiveTest, option) {
  //
  auto ret1 = this->pool().createOptionType(this->pool().get(TYPE::UnwrapError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("w1", "UnwrapError?", HandleKind::TYPE_ALIAS));

  //
  ret1 = this->pool().createOptionType(type1);
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_EQ(type1, type2);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("w2", type2));

  //
  ret1 = this->pool().createArrayType(type2);
  ASSERT_TRUE(ret1);
  ret1 = this->pool().createOptionType(*ret1.asOk());
  ASSERT_TRUE(ret1);
  auto &type3 = *ret1.asOk();
  ASSERT_TRUE(type3.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", type3));
}

TEST_F(ArchiveTest, func) {
  //
  std::vector<const Type *> types;
  auto ret1 = this->pool().createFuncType(this->pool().get(TYPE::OptNothing), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("e1", type1, HandleKind::VAR, HandleAttr::READ_ONLY));

  //
  types = std::vector<const Type *>();
  types.push_back(this->pool().getType("(Signal) -> Void"));
  types.push_back(&type1);
  ret1 = this->pool().createTupleType(std::vector<const Type *>(types));
  ASSERT_TRUE(ret1);
  types.push_back(ret1.asOk());
  ret1 = this->pool().createFuncType(this->pool().get(TYPE::Nothing), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", type2, HandleKind::TYPE_ALIAS));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("DDD", type2));
}

TEST_F(ArchiveTest, mod1) { ASSERT_NO_FATAL_FAILURE(this->archiveMod()); }

TEST_F(ArchiveTest, mod2) {
  //
  std::vector<const Type *> types;
  auto ret1 = this->pool().createFuncType(this->pool().get(TYPE::Void), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_NO_FATAL_FAILURE(this->define("AAA", type1, HandleKind::VAR, HandleAttr::READ_ONLY));

  //
  auto ret2 = this->pool().createOptionType(this->pool().get(TYPE::UnwrapError));
  ASSERT_TRUE(ret2);
  auto &type2 = *ret2.asOk();
  ASSERT_NO_FATAL_FAILURE(
      this->define("a12345", type2, HandleKind::TYPE_ALIAS, HandleAttr::READ_ONLY));

  ASSERT_NO_FATAL_FAILURE(this->archiveMod({"AAA", "a12345"}));
}

TEST_F(ArchiveTest, mod3) {
  {
    auto &modType3 = this->loadMod(false, [](AnalyzerContext &ctx) {
      auto ret1 = ctx.getPool().createMapType(ctx.getPool().get(TYPE::Signal),
                                              ctx.getPool().get(TYPE::GlobError));
      ASSERT_TRUE(ret1);
      auto &type1 = *ret1.asOk();
      ASSERT_NO_FATAL_FAILURE(define(ctx, "AAA", type1, HandleKind::ENV));
      ASSERT_NO_FATAL_FAILURE(
          define(ctx, "BBB", ctx.getPool().get(TYPE::TypeCastError), HandleKind::VAR));
    });
    ASSERT_EQ(3, toUnderlying(modType3.getModId()));
    ASSERT_EQ(1, modType3.getChildSize());
    ASSERT_TRUE(this->newPool().get(modType3.getChildAt(0).typeId()).isModType());

    auto ret1 = this->pool().createArrayType(this->pool().get(TYPE::GlobError));
    ASSERT_TRUE(ret1);
    auto &type1 = *ret1.asOk();
    ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
    ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa", type1));
  }
  ASSERT_NO_FATAL_FAILURE(this->archiveMod());

  //
  auto ret1 = this->newPool().getModTypeById(ModId{3});
  ASSERT_TRUE(ret1);
  auto &modType3 = *ret1;
  auto handle = modType3.lookup(this->newPool(), "AAA");
  ASSERT_TRUE(handle);
  ASSERT_EQ(this->newPool().getType("[Signal : GlobError]")->typeId(), handle->getTypeId());
  ASSERT_EQ(toString(HandleKind::ENV), toString(handle->getKind()));
  ASSERT_EQ(toString(HandleAttr::GLOBAL), toString(handle->attr()));
  ASSERT_EQ(modType3.getModId(), handle->getModId());

  handle = modType3.lookup(this->newPool(), "BBB");
  ASSERT_TRUE(handle);
  ASSERT_EQ(this->newPool().getType("TypeCastError")->typeId(), handle->getTypeId());
  ASSERT_EQ(toString(HandleAttr::GLOBAL), toString(handle->attr()));
  ASSERT_EQ(modType3.getModId(), handle->getModId());

  //
  ret1 = this->newPool().getModTypeById(ModId{1});
  ASSERT_TRUE(ret1);
  auto &modType1 = *ret1;
  ASSERT_EQ(1, toUnderlying(modType1.getModId()));
  ASSERT_EQ(2, modType1.getChildSize());
  ASSERT_TRUE(modType1.getChildAt(0).isGlobal());
  ASSERT_FALSE(modType1.getChildAt(1).isGlobal());
  ASSERT_EQ(modType3.typeId(), modType1.getChildAt(1).typeId());

  handle = modType1.lookup(this->newPool(), "AAA");
  ASSERT_FALSE(handle);

  handle = modType1.lookup(this->newPool(), "aaa");
  ASSERT_TRUE(handle);
  ASSERT_EQ(this->newPool().getType("[GlobError]")->typeId(), handle->getTypeId());
  ASSERT_EQ(toString(HandleAttr::GLOBAL), toString(handle->attr()));
  ASSERT_EQ(1, toUnderlying(handle->getModId()));
}

TEST_F(ArchiveTest, mod4) {
  {
    auto &modType3 = this->loadMod(false, [&](AnalyzerContext &ctx1) {
      auto &modType4 = this->loadModAt(ctx1, true, [](AnalyzerContext &ctx2) {
        auto ret = ctx2.getPool().createArrayType(ctx2.getPool().get(TYPE::Bool));
        ASSERT_TRUE(ret);
        ctx2.getScope()->defineTypeAlias(ctx2.getPool(), "BoolArray", *ret.asOk());
        ASSERT_NO_FATAL_FAILURE(
            define(ctx2, "AAA", *ret.asOk(), HandleKind::VAR, HandleAttr::READ_ONLY));
      });
      ASSERT_EQ(4, toUnderlying(modType4.getModId()));

      auto ret = ctx1.getPool().createTupleType(
          {&ctx1.getPool().get(TYPE::IllegalAccessError), &modType4});
      ASSERT_TRUE(ret);
      ASSERT_NO_FATAL_FAILURE(
          define(ctx1, "BBB", *ret.asOk(), HandleKind::VAR, HandleAttr::READ_ONLY));
    });
    ASSERT_NO_FATAL_FAILURE(define("CCC", modType3, HandleKind::VAR, HandleAttr::READ_ONLY));

    ASSERT_EQ(3, toUnderlying(modType3.getModId()));
    ASSERT_EQ(2, modType3.getChildSize());
  }
  ASSERT_NO_FATAL_FAILURE(this->archiveMod({"CCC"}));

  auto ret = this->newPool().getModTypeById(ModId{3});
  ASSERT_TRUE(ret);
  auto &modType3 = *ret;
  ASSERT_EQ(3, toUnderlying(modType3.getModId()));
  ASSERT_EQ(2, modType3.getChildSize());
  auto handle = modType3.lookup(this->newPool(), "BBB");
  ASSERT_TRUE(handle);
  ASSERT_EQ(3, toUnderlying(handle->getModId()));
  auto &type1 = this->newPool().get(handle->getTypeId());
  ASSERT_TRUE(type1.isTupleType());
  auto &tuple = cast<TupleType>(type1);
  ASSERT_EQ(2, tuple.getFieldSize());
  ASSERT_EQ(this->newPool().get(TYPE::IllegalAccessError),
            tuple.getFieldTypeAt(this->newPool(), 0));
  ASSERT_TRUE(tuple.getFieldTypeAt(this->newPool(), 1).isModType());
  ASSERT_EQ(4, toUnderlying(cast<ModType>(tuple.getFieldTypeAt(this->newPool(), 1)).getModId()));

  handle = modType3.lookup(this->newPool(), "AAA");
  ASSERT_FALSE(handle);
  auto *handle2 = modType3.lookupVisibleSymbolAtModule(this->newPool(), "AAA");
  ASSERT_TRUE(handle2);
  ASSERT_EQ(4, toUnderlying(handle2->getModId()));
  auto retType = this->newPool().getType("[Bool]");
  ASSERT_TRUE(retType);
  ASSERT_EQ(retType->typeId(), handle2->getTypeId());
}

TEST_F(ArchiveTest, userdefined) {
  auto &modType = this->loadMod(false, [&](AnalyzerContext &ctx) { // named import
    const auto modId = ctx.getModId();
    const char *typeName = "APIError";
    auto ret = ctx.getPool().createErrorType(typeName, ctx.getPool().get(TYPE::Error), modId);
    ASSERT_TRUE(ret);
    auto ret2 = ctx.getScope()->defineTypeAlias(ctx.getPool(), typeName, *ret.asOk());
    ASSERT_TRUE(ret2);

    typeName = "Interval";
    ret = ctx.getPool().createRecordType(typeName, modId);
    ASSERT_TRUE(ret);
    ret2 = ctx.getScope()->defineTypeAlias(ctx.getPool(), typeName, *ret.asOk());
    ASSERT_TRUE(ret2);
    {
      auto &recordType = cast<RecordType>(*ret.asOk());
      auto ret3 = ctx.getScope()->defineConstructor(
          ctx.getPool(), recordType, {&ctx.getPool().get(TYPE::Int), &ctx.getPool().get(TYPE::Int)},
          packedParamNames({"_begin", "_end"}));
      ASSERT_TRUE(ret3);
      ASSERT_EQ("_begin;_end", cast<MethodHandle>(*ret3.asOk()).getPackedParamNames().toString());

      std::unordered_map<std::string, HandlePtr> handles;
      handles.emplace(
          "begin", HandlePtr::create(recordType, 0, HandleKind::VAR, HandleAttr::READ_ONLY, modId));
      auto *type = ctx.getPool().getType(toQualifiedTypeName("APIError", modId));
      handles.emplace("end",
                      HandlePtr::create(*type, 1, HandleKind::VAR, HandleAttr::READ_ONLY, modId));
      ctx.getPool().finalizeRecordType(recordType, std::move(handles));
    }

    typeName = "_Pair";
    ret = ctx.getPool().createRecordType(typeName, modId);
    ASSERT_TRUE(ret);
    ret2 = ctx.getScope()->defineTypeAlias(ctx.getPool(), typeName, *ret.asOk());
    ASSERT_TRUE(ret2);
  });

  //
  const char *typeName = "APIError";
  auto handle = modType.lookupField(this->pool(), toTypeAliasFullName(typeName));
  ASSERT_TRUE(handle);
  auto typeOrError = this->pool().getType(toQualifiedTypeName(typeName, modType.getModId()));
  ASSERT_TRUE(typeOrError);
  ASSERT_EQ(handle->getTypeId(), typeOrError->typeId());
  auto handle2 = this->scope().lookup(toTypeAliasFullName(typeName));
  ASSERT_FALSE(handle2); // in named import, not found
  ASSERT_EQ(NameLookupError::NOT_FOUND, handle2.asErr());
  auto lookup = this->scope().lookupField(this->pool(), modType, toTypeAliasFullName(typeName));
  ASSERT_TRUE(lookup);
  ASSERT_EQ(*typeOrError, this->pool().get(lookup.asOk()->getTypeId()));

  //
  typeName = "Interval";
  handle = modType.lookupField(this->pool(), toTypeAliasFullName(typeName));
  ASSERT_TRUE(handle);
  typeOrError = this->pool().getType(toQualifiedTypeName(typeName, modType.getModId()));
  ASSERT_TRUE(typeOrError);
  ASSERT_EQ(handle->getTypeId(), typeOrError->typeId());
  handle2 = this->scope().lookup(toTypeAliasFullName(typeName));
  ASSERT_FALSE(handle2); // in named import, not found
  ASSERT_EQ(NameLookupError::NOT_FOUND, handle2.asErr());
  lookup = this->scope().lookupField(this->pool(), modType, toTypeAliasFullName(typeName));
  ASSERT_TRUE(lookup);
  ASSERT_EQ(*typeOrError, this->pool().get(lookup.asOk()->getTypeId()));
  {
    auto &recordType = cast<RecordType>(*typeOrError);
    ASSERT_TRUE(recordType.isFinalized());
    ASSERT_EQ(2, recordType.getFieldSize());
    ASSERT_EQ(2, recordType.getHandleMap().size());
    auto hd = recordType.lookupField("begin");
    ASSERT_TRUE(hd);
    ASSERT_EQ(recordType, this->pool().get(hd->getTypeId()));
    hd = recordType.lookupField("end");
    ASSERT_TRUE(hd);
    ASSERT_EQ(*this->pool().getType(toQualifiedTypeName("APIError", modType.getModId())),
              this->pool().get(hd->getTypeId()));
  }
  auto *methodHandle = this->scope().lookupConstructor(this->pool(), *typeOrError);
  ASSERT_TRUE(methodHandle);
  ASSERT_TRUE(methodHandle->isConstructor());
  ASSERT_EQ(2, methodHandle->getParamSize());
  ASSERT_EQ(this->pool().get(TYPE::Int), methodHandle->getParamTypeAt(0));
  ASSERT_EQ(this->pool().get(TYPE::Int), methodHandle->getParamTypeAt(1));
  ASSERT_EQ("_begin;_end", methodHandle->getPackedParamNames().toString());
  ASSERT_EQ(*typeOrError, methodHandle->getReturnType());
  ASSERT_EQ(*typeOrError, this->pool().get(methodHandle->getRecvTypeId()));
  ASSERT_TRUE(methodHandle->has(HandleAttr::READ_ONLY | HandleAttr::GLOBAL));

  //
  typeName = "_Pair";
  handle = modType.lookupField(this->pool(), toTypeAliasFullName(typeName));
  ASSERT_TRUE(handle);
  typeOrError = this->pool().getType(toQualifiedTypeName(typeName, modType.getModId()));
  ASSERT_TRUE(typeOrError);
  ASSERT_EQ(handle->getTypeId(), typeOrError->typeId());
  /**
   * after deserialized, always true
   */
  ASSERT_TRUE(cast<RecordType>(typeOrError)->isFinalized());
  handle2 = this->scope().lookup(toTypeAliasFullName(typeName));
  ASSERT_FALSE(handle2); // in named import, not found
  ASSERT_EQ(NameLookupError::NOT_FOUND, handle2.asErr());
  lookup = this->scope().lookupField(this->pool(), modType, toTypeAliasFullName(typeName));
  ASSERT_FALSE(lookup); // module private member is not found
}

TEST_F(ArchiveTest, function) {
  /**
   * for named import
   */
  auto &modType = this->loadMod(false, [&](AnalyzerContext &ctx) {
    // no args
    auto typeOrError = ctx.getPool().createFuncType(ctx.getPool().get(TYPE::Int), {});
    ASSERT_TRUE(typeOrError);
    auto ret = ctx.getScope()->defineNamedFunction("get", cast<FunctionType>(*typeOrError.asOk()),
                                                   packedParamNames({}));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.asOk()->isFuncHandle());
    ASSERT_TRUE(ret.asOk()->has(HandleAttr::GLOBAL | HandleAttr::READ_ONLY));
    ASSERT_EQ("", cast<FuncHandle>(*ret.asOk()).getPackedParamNames().toString());

    // one arg
    typeOrError =
        ctx.getPool().createFuncType(ctx.getPool().get(TYPE::Int), {&ctx.getPool().get(TYPE::Int)});
    ASSERT_TRUE(typeOrError);
    ret = ctx.getScope()->defineNamedFunction("inc", cast<FunctionType>(*typeOrError.asOk()),
                                              packedParamNames({"value"}));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.asOk()->isFuncHandle());
    ASSERT_TRUE(ret.asOk()->has(HandleAttr::GLOBAL | HandleAttr::READ_ONLY));
    ASSERT_EQ("value", cast<FuncHandle>(*ret.asOk()).getPackedParamNames().toString());

    // tow arg
    typeOrError = ctx.getPool().createFuncType(
        ctx.getPool().get(TYPE::String),
        {&ctx.getPool().get(TYPE::Int), &ctx.getPool().get(TYPE::Float)});
    ASSERT_TRUE(typeOrError);
    ret = ctx.getScope()->defineNamedFunction("compute", cast<FunctionType>(*typeOrError.asOk()),
                                              packedParamNames({"left", "right"}));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.asOk()->isFuncHandle());
    ASSERT_TRUE(ret.asOk()->has(HandleAttr::GLOBAL | HandleAttr::READ_ONLY));
    ASSERT_EQ("left;right", cast<FuncHandle>(*ret.asOk()).getPackedParamNames().toString());
  });
  (void)modType;

  auto handle = this->scope().lookupField(this->pool(), modType, "get");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(handle.asOk()->isFuncHandle());
  ASSERT_TRUE(handle.asOk()->has(HandleAttr::GLOBAL | HandleAttr::READ_ONLY));
  ASSERT_EQ("", cast<FuncHandle>(*handle.asOk()).getPackedParamNames().toString());

  handle = this->scope().lookupField(this->pool(), modType, "inc");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(handle.asOk()->isFuncHandle());
  ASSERT_TRUE(handle.asOk()->has(HandleAttr::GLOBAL | HandleAttr::READ_ONLY));
  ASSERT_EQ("value", cast<FuncHandle>(*handle.asOk()).getPackedParamNames().toString());

  handle = this->scope().lookupField(this->pool(), modType, "compute");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(handle.asOk()->isFuncHandle());
  ASSERT_TRUE(handle.asOk()->has(HandleAttr::GLOBAL | HandleAttr::READ_ONLY));
  ASSERT_EQ("left;right", cast<FuncHandle>(*handle.asOk()).getPackedParamNames().toString());
}

TEST_F(ArchiveTest, method1) {
  /**
   * for named import
   */
  auto &modType = this->loadMod(false, [&](AnalyzerContext &ctx) {
    auto ret = ctx.getScope()->defineMethod(
        ctx.getPool(), ctx.getPool().get(TYPE::Int), "sum", ctx.getPool().get(TYPE::Int),
        {&ctx.getPool().get(TYPE::Int)}, packedParamNames({"right"}));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.asOk()->isMethodHandle());
    ASSERT_EQ("right", cast<MethodHandle>(*ret.asOk()).getPackedParamNames().toString());

    ret = ctx.getScope()->defineMethod(ctx.getPool(), ctx.getPool().get(TYPE::Int), "_value",
                                       ctx.getPool().get(TYPE::Int), {}, packedParamNames({}));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.asOk()->isMethodHandle());
    ASSERT_EQ("", cast<MethodHandle>(*ret.asOk()).getPackedParamNames().toString());
  });
  (void)modType;

  auto method = this->scope().lookupMethod(this->pool(), this->pool().get(TYPE::Int), "sum");
  ASSERT_FALSE(method);

  method = this->scope().lookupMethod(this->pool(), this->pool().get(TYPE::Int), "_value");
  ASSERT_FALSE(method);
}

TEST_F(ArchiveTest, method2) {
  /**
   * for global import
   */
  auto &modType = this->loadMod(true, [&](AnalyzerContext &ctx) {
    auto ret = ctx.getScope()->defineMethod(
        ctx.getPool(), ctx.getPool().get(TYPE::Int), "sum", ctx.getPool().get(TYPE::Int),
        {&ctx.getPool().get(TYPE::Int)}, packedParamNames({"target"}));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.asOk()->isMethodHandle());
    ASSERT_EQ("target", cast<MethodHandle>(*ret.asOk()).getPackedParamNames().toString());

    ret = ctx.getScope()->defineMethod(ctx.getPool(), ctx.getPool().get(TYPE::Int), "_value",
                                       ctx.getPool().get(TYPE::Int), {}, packedParamNames({}));
    ASSERT_TRUE(ret);
    ASSERT_TRUE(ret.asOk()->isMethodHandle());
  });
  (void)modType;

  auto methodorError = this->scope().lookupMethod(this->pool(), this->pool().get(TYPE::Int), "sum");
  ASSERT_TRUE(methodorError);
  auto *method = methodorError.asOk();
  ASSERT_TRUE(method->isMethodHandle());
  ASSERT_EQ(1, method->getParamSize());
  ASSERT_EQ(this->pool().get(TYPE::Int), method->getParamTypeAt(0));
  ASSERT_EQ(this->pool().get(TYPE::Int), this->pool().get(method->getRecvTypeId()));
  ASSERT_EQ(this->pool().get(TYPE::Int), method->getReturnType());
  ASSERT_EQ("target", method->getPackedParamNames().toString());

  methodorError = this->scope().lookupMethod(this->pool(), this->pool().get(TYPE::Int), "_value");
  ASSERT_FALSE(methodorError);
}

TEST_F(ArchiveTest, argEntry) {
  /**
   * for global import
   */
  auto &modType = this->loadMod(true, [&](AnalyzerContext &ctx) {
    ArgEntriesBuilder builder;
    builder
        .add([](ArgEntry &e) {
          e.setParseOp(OptParseOp::NO_ARG);
          e.setShortName('e');
          e.setLongName("enable");
          e.setDetail("enable features");
        })
        .add(1,
             [](ArgEntry &e) {
               e.setParseOp(OptParseOp::NO_ARG);
               e.setShortName('d');
               e.setLongName("disable");
               e.setAttr(ArgEntryAttr::STORE_FALSE);
               e.setDetail("disable features");
             })
        .add([](ArgEntry &e) {
          e.setParseOp(OptParseOp::OPT_ARG);
          e.setShortName('o');
          e.setLongName("output");
          e.setArgName("target");
          e.setDefaultValue("/dev/stdout");
          e.setAttr(ArgEntryAttr::REQUIRED);
        })
        .add([](ArgEntry &e) {
          e.setParseOp(OptParseOp::HAS_ARG);
          e.setShortName('t');
          e.setLongName("timeout");
          e.setIntRange(0, INT64_MAX);
        })
        .add([](ArgEntry &e) {
          e.setParseOp(OptParseOp::HAS_ARG);
          e.setLongName("log");
          e.setArgName("level");
          e.addChoice(strdup("info"));
          e.addChoice(strdup("warn"));
        })
        .add([](ArgEntry &e) {
          e.setArgName("files");
          e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REMAIN | ArgEntryAttr::REQUIRED);
        });

    auto &recordType = createRecordType(ctx.getPool(), "type1", std::move(builder), ctx.getModId(),
                                        CLIRecordType::Attr::VERBOSE, "sample program");
    ASSERT_EQ(6, recordType.getEntries().size());
    auto ret = ctx.getScope()->defineTypeAlias(ctx.getPool(), "type1", recordType);
    ASSERT_TRUE(ret);

    define(ctx, "cli", recordType);
  });
  (void)modType;

  auto ret = this->scope().lookup("cli");
  ASSERT_TRUE(ret);
  ASSERT_TRUE(isa<CLIRecordType>(this->pool().get(ret.asOk()->getTypeId())));
  auto &recordType = cast<CLIRecordType>(this->pool().get(ret.asOk()->getTypeId()));
  auto &entries = recordType.getEntries();
  ASSERT_EQ(6, entries.size());
  ASSERT_EQ(CLIRecordType::Attr::VERBOSE, recordType.getAttr());
  ASSERT_EQ("sample program", recordType.getDesc());

  // -e --enable
  ASSERT_EQ(1, entries[0].getFieldOffset());
  ASSERT_EQ(OptParseOp::NO_ARG, entries[0].getParseOp());
  ASSERT_EQ(ArgEntryAttr{}, entries[0].getAttr());
  ASSERT_EQ('e', entries[0].getShortName());
  ASSERT_EQ("enable", entries[0].getLongName());
  ASSERT_TRUE(entries[0].getArgName().empty());
  ASSERT_TRUE(entries[0].getDefaultValue().empty());
  ASSERT_EQ("enable features", entries[0].getDetail());
  ASSERT_EQ(ArgEntry::CheckerKind::NOP, entries[0].getCheckerKind());

  // -d --disable
  ASSERT_EQ(1, entries[1].getFieldOffset());
  ASSERT_EQ(OptParseOp::NO_ARG, entries[1].getParseOp());
  ASSERT_EQ(ArgEntryAttr::STORE_FALSE, entries[1].getAttr());
  ASSERT_EQ('d', entries[1].getShortName());
  ASSERT_EQ("disable", entries[1].getLongName());
  ASSERT_TRUE(entries[1].getArgName().empty());
  ASSERT_TRUE(entries[1].getDefaultValue().empty());
  ASSERT_EQ("disable features", entries[1].getDetail());
  ASSERT_EQ(ArgEntry::CheckerKind::NOP, entries[1].getCheckerKind());

  // -o --output
  ASSERT_EQ(2, entries[2].getFieldOffset());
  ASSERT_EQ(OptParseOp::OPT_ARG, entries[2].getParseOp());
  ASSERT_EQ(ArgEntryAttr::REQUIRED, entries[2].getAttr());
  ASSERT_EQ('o', entries[2].getShortName());
  ASSERT_EQ("output", entries[2].getLongName());
  ASSERT_EQ("target", entries[2].getArgName());
  ASSERT_EQ("/dev/stdout", entries[2].getDefaultValue());
  ASSERT_TRUE(entries[2].getDetail().empty());
  ASSERT_EQ(ArgEntry::CheckerKind::NOP, entries[2].getCheckerKind());

  // -t  --timeout
  ASSERT_EQ(3, entries[3].getFieldOffset());
  ASSERT_EQ(OptParseOp::HAS_ARG, entries[3].getParseOp());
  ASSERT_EQ(ArgEntryAttr{}, entries[3].getAttr());
  ASSERT_EQ('t', entries[3].getShortName());
  ASSERT_EQ("timeout", entries[3].getLongName());
  ASSERT_TRUE(entries[3].getArgName().empty());
  ASSERT_TRUE(entries[3].getDefaultValue().empty());
  ASSERT_TRUE(entries[3].getDetail().empty());
  ASSERT_EQ(ArgEntry::CheckerKind::INT, entries[3].getCheckerKind());
  {
    auto [min, max] = entries[3].getIntRange();
    ASSERT_EQ(0, min);
    ASSERT_EQ(INT64_MAX, max);
  }

  // --log
  ASSERT_EQ(4, entries[4].getFieldOffset());
  ASSERT_EQ(OptParseOp::HAS_ARG, entries[4].getParseOp());
  ASSERT_EQ(ArgEntryAttr{}, entries[4].getAttr());
  ASSERT_EQ('\0', entries[4].getShortName());
  ASSERT_EQ("log", entries[4].getLongName());
  ASSERT_EQ("level", entries[4].getArgName());
  ASSERT_TRUE(entries[4].getDefaultValue().empty());
  ASSERT_TRUE(entries[4].getDetail().empty());
  ASSERT_EQ(ArgEntry::CheckerKind::CHOICE, entries[4].getCheckerKind());
  {
    auto &choice = entries[4].getChoice();
    ASSERT_EQ(2, choice.size());
    ASSERT_STREQ("info", choice[0]);
    ASSERT_STREQ("warn", choice[1]);
  }

  // positional
  ASSERT_EQ(5, entries[5].getFieldOffset());
  ASSERT_EQ(OptParseOp::NO_ARG, entries[5].getParseOp());
  ASSERT_EQ(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REMAIN | ArgEntryAttr::REQUIRED,
            entries[5].getAttr());
  ASSERT_EQ('\0', entries[5].getShortName());
  ASSERT_TRUE(entries[5].getLongName().empty());
  ASSERT_EQ("files", entries[5].getArgName());
  ASSERT_TRUE(entries[5].getDefaultValue().empty());
  ASSERT_TRUE(entries[5].getDetail().empty());
  ASSERT_EQ(ArgEntry::CheckerKind::NOP, entries[5].getCheckerKind());
}

struct Builder {
  SourceManager &srcMan;
  ModuleArchives &archives;

  template <typename... Args>
  ModuleArchivePtr operator()(const char *path, Args &&...args) {
    std::vector<Archive> handles;
    std::vector<ModuleArchive::Imported> deps;
    build(deps, std::forward<Args>(args)...);
    auto src = this->srcMan.update(path, 0, "");
    auto archive = std::make_shared<ModuleArchive>(src->getSrcId(), ModAttr{}, std::move(handles),
                                                   std::move(deps));
    this->archives.add(archive);
    return archive;
  }

private:
  template <typename... Args>
  static void build(std::vector<ModuleArchive::Imported> &deps, const ModuleArchivePtr &archive,
                    Args &&...args) {
    deps.emplace_back(ModuleArchive::Imported::create(ImportedModKind::GLOBAL, archive->getModId(),
                                                      archive->getHash()));
    build(deps, std::forward<Args>(args)...);
  }

  static void build(std::vector<ModuleArchive::Imported> &) {}
};

TEST(ArchivesTest, revert1) {
  SourceManager srcMan;
  ModuleArchives archives;
  Builder builder = {
      .srcMan = srcMan,
      .archives = archives,
  };

  auto t1 = builder("/aaa");
  auto t3 = builder("/ccc", t1);
  auto t4 = builder("/ddd");
  auto t2 = builder("/bbb", t3, t4);
  ASSERT_TRUE(archives.find(t1->getModId()));
  ASSERT_TRUE(archives.find(t2->getModId()));
  ASSERT_TRUE(archives.find(t3->getModId()));
  ASSERT_TRUE(archives.find(t4->getModId()));

  archives.revert({archives.find(srcMan.find("/ddd")->getSrcId())->getModId()});
  ASSERT_EQ(1, toUnderlying(archives.find(srcMan.find("/aaa")->getSrcId())->getModId()));
  ASSERT_EQ(2, toUnderlying(archives.find(srcMan.find("/ccc")->getSrcId())->getModId()));
  ASSERT_FALSE(archives.find(t2->getModId()));
  ASSERT_FALSE(archives.find(t4->getModId()));

  archives.revert({archives.find(srcMan.find("/aaa")->getSrcId())->getModId()});
  ASSERT_FALSE(archives.find(t1->getModId()));
  ASSERT_FALSE(archives.find(t2->getModId()));
  ASSERT_FALSE(archives.find(t3->getModId()));
  ASSERT_FALSE(archives.find(t4->getModId()));
}

TEST(ArchivesTest, revert2) {
  SourceManager srcMan;
  ModuleArchives archives;
  Builder builder = {
      .srcMan = srcMan,
      .archives = archives,
  };

  auto t1 = builder("/aaa");
  auto t3 = builder("/ccc", t1);
  auto t4 = builder("/ddd");
  auto t2 = builder("/bbb", t3, t4);
  auto t5 = builder("/eee");
  ASSERT_TRUE(archives.find(t1->getModId()));
  ASSERT_TRUE(archives.find(t2->getModId()));
  ASSERT_TRUE(archives.find(t3->getModId()));
  ASSERT_TRUE(archives.find(t4->getModId()));
  ASSERT_TRUE(archives.find(t5->getModId()));

  archives.removeIfUnused(t1->getModId());
  ASSERT_TRUE(archives.find(t1->getModId()));

  archives.removeIfUnused(t4->getModId());
  ASSERT_TRUE(archives.find(t4->getModId()));

  archives.removeIfUnused(t5->getModId());
  ASSERT_FALSE(archives.find(t5->getModId()));
  archives.removeIfUnused(t5->getModId());
  ASSERT_FALSE(archives.find(t5->getModId()));
  ASSERT_EQ(nullptr, archives.find(srcMan.find("/eee")->getSrcId()));

  archives.revert({t1->getModId(), t4->getModId()});
  ASSERT_FALSE(archives.find(t1->getModId()));
  ASSERT_FALSE(archives.find(t2->getModId()));
  ASSERT_FALSE(archives.find(t3->getModId()));
  ASSERT_FALSE(archives.find(t4->getModId()));
  ASSERT_FALSE(archives.find(t5->getModId()));
}

class ArchiveHashTest : public IndexTest {
public:
  void doAnalyze(const char *content, ModId &modId) {
    unsigned short tmp;
    IndexTest::doAnalyze(content, tmp);
    modId = ModId{tmp};
  }

  void updateSource(ModId modId, const char *newContent) {
    auto src = this->srcMan.findById(modId);
    this->updateSource(std::move(src), newContent);
  }

  void updateSource(StringRef path, const char *newContent) {
    auto src = this->srcMan.find(path);
    this->updateSource(std::move(src), newContent);
  }

private:
  void updateSource(SourcePtr &&src, const char *newContent) {
    ASSERT_TRUE(src);
    const ModId modId = src->getSrcId();
    src = this->srcMan.update(src->getPath(), src->getVersion() + 1, newContent);
    ASSERT_TRUE(src);
    ASSERT_TRUE(this->indexes.remove(modId));
    this->archives.revert({modId});

    // rebuild
    auto ret = this->analyze(src);
    ASSERT_TRUE(ret);
    ASSERT_EQ(ret->getModId(), modId);
    ASSERT_TRUE(this->indexes.find(modId));
  }
};

TEST_F(ArchiveHashTest, base) {
  ModId modId;
  const char *content = R"E(
var AAA = 2345


  typedef Interval(aaa:Int, bbb:Int) {
  let begin = $aaa
  let end = $bbb
}
new Interval($bbb: 34, $aaa: 2)
)E";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId));

  auto archive1 = this->archives.find(modId);
  auto src1 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive1);
  ASSERT_TRUE(src1);

  // remove spaces
  content = R"E(
var AAA = 2345
typedef Interval(aaa:Int, bbb:Int) {
  let begin = $aaa
  let end = $bbb
}
new Interval($bbb: 34, $aaa: 2)
)E";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content));
  auto archive2 = this->archives.find(modId);
  auto src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_TRUE(src2);
  ASSERT_EQ(archive1->getModId(), archive2->getModId());
  ASSERT_EQ(archive1->getHash(), archive2->getHash());
  ASSERT_TRUE(archive1->equalsDigest(*archive2));

  // change remain expression
  content = R"E(
var AAA = 2345
typedef Interval(aaa:Int, bbb:Int) {
  let begin = $aaa
  let end = $bbb
}
new Interval($bbb: 34, $aaa: 2).begin + 23455432 + [$false].size()
ls -la
)E";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content));
  archive2 = this->archives.find(modId);
  src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_EQ(archive1->getModId(), archive2->getModId());
  ASSERT_EQ(archive1->getHash(), archive2->getHash());
  ASSERT_TRUE(archive1->equalsDigest(*archive2));

  // add private (hash is modified due to global symbol addition)
  content = R"E(
  var AAA = 2345
  typedef Interval(aaa:Int, bbb:Int) {
    let begin = $aaa
    let end = $bbb
  }

  function _sum(a: Int, bb: Int): Int {
    return [$a + $bb][0]
  }

  )E";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content));
  archive2 = this->archives.find(modId);
  src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_EQ(archive1->getModId(), archive2->getModId());
  ASSERT_NE(archive1->getHash(), archive2->getHash());
  ASSERT_FALSE(archive1->equalsDigest(*archive2));

  // reorder symbol (hash is modified due to global symbol reordering) //TODO: order independent?
  content = R"E(
  typedef Interval(aaa:Int, bbb:Int) {
    let begin = $aaa
    let end = $bbb
  }
  var AAA = 2345
  )E";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content));
  archive2 = this->archives.find(modId);
  src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_EQ(archive1->getModId(), archive2->getModId());
  ASSERT_NE(archive1->getHash(), archive2->getHash());
  ASSERT_FALSE(archive1->equalsDigest(*archive2));
}

TEST_F(ArchiveHashTest, field) {
  ModId modId;
  const char *content = R"E(
var AAA = 2345

typedef Interval(aaa:Int, bbb:Int) {
  let begin = $aaa
  type INT = Int
  let end = $bbb
  type FLOAT = Float
}
new Interval($bbb: 34, $aaa: 2)
)E";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId));

  auto archive1 = this->archives.find(modId);
  auto src1 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive1);
  ASSERT_TRUE(src1);

  // reorder type alias within field
  content = R"E(
var AAA = 2345

typedef Interval(aaa:Int, bbb:Int) {
  let begin = $aaa
  let end = $bbb
  type FLOAT = Float

  type INT = Int
}
new Interval($bbb: 34, $aaa: 2)
)E";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content));
  auto archive2 = this->archives.find(modId);
  auto src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_TRUE(src2);
  ASSERT_EQ(archive1->getModId(), archive2->getModId());
  ASSERT_EQ(archive1->getHash(), archive2->getHash());
  ASSERT_TRUE(archive1->equalsDigest(*archive2));

  // modify constructor
  content = R"E(
var AAA = 2345

typedef Interval(aaa:Int, bbb:Int) {
  type INT = Int
  let begin = $aaa
  let end = $bbb

  {   ls -la $begin $end; var aaa = 2345;}
  type FLOAT = Float
}
new Interval($bbb: 34, $aaa: 2)
)E";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content));
  archive2 = this->archives.find(modId);
  src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_EQ(archive1->getModId(), archive2->getModId());
  ASSERT_EQ(archive1->getHash(), archive2->getHash());
  ASSERT_TRUE(archive1->equalsDigest(*archive2));
}

TEST_F(ArchiveHashTest, load) {
  TempFileFactory tempFileFactory("arsh_archive");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
typedef FFF(bb:String?) { typedef GGG = Error; }
function HHH(cc: Int?) for FFF {}
type III { let begin: Int; let end: Int; }
)");

  ModId modId;
  auto content = format(R"(
source %s
new FFF.GGG('34')
new FFF($bb:'').HHH($cc:22)
new III($end:12, $begin:11).end
)",
                        fileName.c_str());

  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content.c_str(), modId));

  auto archive1 = this->archives.find(modId);
  auto archiveImported1 = this->archives.find(this->srcMan.find(fileName)->getSrcId());
  auto src1 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive1);
  ASSERT_TRUE(src1);
  ASSERT_EQ(ModAttr{}, archive1->getModAttr());

  // modify imported module (digest has not changed)
  content = R"(
typedef FFF(bb:String?) { typedef GGG = Error; }
function HHH(cc: Int?) for FFF { var aaa = $cc; }
type III { let begin: Int; let end: Int; }
)";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(fileName, content.c_str()));
  content = format(R"(
source %s
new FFF.GGG('34')

new FFF($bb:'').HHH($cc:22)
new III($end:12, $begin:11).end
)",
                   fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content.c_str()));

  auto archive2 = this->archives.find(modId);
  auto archiveImported2 = this->archives.find(this->srcMan.find(fileName)->getSrcId());
  auto src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_TRUE(src2);
  ASSERT_EQ(archive2->getModId(), archive1->getModId());
  ASSERT_EQ(archive2->getHash(), archive1->getHash());
  ASSERT_EQ(archiveImported2->getHash(), archiveImported1->getHash());

  // change import type
  content = format(R"(
source %s inlined
new FFF.GGG('34')

new FFF($bb:'').HHH($cc:22)
new III($end:12, $begin:11).end
)",
                   fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content.c_str()));

  archive2 = this->archives.find(modId);
  src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_TRUE(src2);
  ASSERT_EQ(archive2->getModId(), archive1->getModId());
  ASSERT_NE(archive2->getHash(), archive1->getHash());
  ASSERT_EQ(archiveImported2->getHash(), archiveImported1->getHash());

  // has error (global symbol signatures have not changed)
  content = format(R"(
source %s
new FFF.GGG('34')
new FFF($bb:'').HHH($cc:22)
new III($end:12, $begin:11).end
2345/ ""
)",
                   fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content.c_str()));

  archive2 = this->archives.find(modId);
  archiveImported2 = this->archives.find(this->srcMan.find(fileName)->getSrcId());
  src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_TRUE(src2);
  ASSERT_EQ(archive2->getModId(), archive1->getModId());
  ASSERT_NE(archive2->getHash(), archive1->getHash());
  ASSERT_EQ(archiveImported2->getHash(), archiveImported1->getHash());

  // modify imported module (digest has changed)
  content = R"(
typedef FFF(bb:String?) { typedef GGG = Error; }
function HHH(cc: Int?) for FFF { var aaa = $cc; }
type III { let begin: Int; let end: Int; }
var AAA = 2345
)";
  ASSERT_NO_FATAL_FAILURE(this->updateSource(fileName, content.c_str()));
  content = format(R"(
source %s
new FFF.GGG('34')

new FFF($bb:'').HHH($cc:22)
new III($end:12, $begin:11).end
)",
                   fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(this->updateSource(modId, content.c_str()));

  archive2 = this->archives.find(modId);
  archiveImported2 = this->archives.find(this->srcMan.find(fileName)->getSrcId());
  src2 = this->srcMan.findById(modId);
  ASSERT_TRUE(archive2);
  ASSERT_TRUE(src2);
  ASSERT_EQ(archive2->getModId(), archive1->getModId());
  ASSERT_NE(archive2->getHash(), archive1->getHash());
  ASSERT_NE(archiveImported2->getHash(), archiveImported1->getHash());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}