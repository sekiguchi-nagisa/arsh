
#include "gtest/gtest.h"

#include "../arg_parser_helper.hpp"

#include "analyzer.h"
#include "archive.h"

using namespace ydsh::lsp;
using namespace ydsh;

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

struct ArchiveBuilder {
  ModId id;
  std::vector<ArchiveBuilder> children;

  ModuleArchivePtr build() const {
    std::vector<Archive> handles;
    std::vector<std::pair<ImportedModKind, ModuleArchivePtr>> imported;
    imported.reserve(this->children.size());
    for (auto &e : this->children) {
      imported.emplace_back(ImportedModKind::GLOBAL, e.build());
    }
    return std::make_shared<ModuleArchive>(this->id, 0, ModAttr{}, std::move(handles),
                                           std::move(imported));
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
  auto t = tree<1>(tree<2>(tree<5>(tree<7>()), tree<6>()), tree<3>(tree<4>()));
  auto archive = t.build();
  ASSERT_EQ(2, archive->getImported().size());
  ASSERT_EQ(2, toUnderlying(archive->getImported()[0].second->getModId()));
  ASSERT_EQ(3, toUnderlying(archive->getImported()[1].second->getModId()));
  auto deps = archive->getDepsByTopologicalOrder();
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(7, toUnderlying(deps[0]->getModId()));
  ASSERT_EQ(5, toUnderlying(deps[1]->getModId()));
  ASSERT_EQ(6, toUnderlying(deps[2]->getModId()));
  ASSERT_EQ(2, toUnderlying(deps[3]->getModId()));
  ASSERT_EQ(4, toUnderlying(deps[4]->getModId()));
  ASSERT_EQ(3, toUnderlying(deps[5]->getModId()));
}

TEST(DependentTest, deps2) {
  auto t1 = tree<1>(tree<2>(), tree<3>(tree<4>()));
  auto t2 = tree<5>(tree<2>(), t1, tree<6>(tree<4>()), tree<3>(tree<4>()));
  auto t3 = tree<7>(t2, t1);
  auto archive = t3.build();
  ASSERT_EQ(2, archive->getImported().size());
  ASSERT_EQ(5, toUnderlying(archive->getImported()[0].second->getModId()));
  ASSERT_EQ(1, toUnderlying(archive->getImported()[1].second->getModId()));
  auto deps = archive->getDepsByTopologicalOrder();
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(2, toUnderlying(deps[0]->getModId()));
  ASSERT_EQ(4, toUnderlying(deps[1]->getModId()));
  ASSERT_EQ(3, toUnderlying(deps[2]->getModId()));
  ASSERT_EQ(1, toUnderlying(deps[3]->getModId()));
  ASSERT_EQ(6, toUnderlying(deps[4]->getModId()));
  ASSERT_EQ(5, toUnderlying(deps[5]->getModId()));
}

TEST(DependentTest, deps3) {
  auto t = tree<1>();
  auto archive = t.build();
  ASSERT_EQ(0, archive->getImported().size());
  auto deps = archive->getDepsByTopologicalOrder();
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
    return std::make_unique<AnalyzerContext>(config, *src);
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

  void defineAndArchive(const char *fieldName, const DSType &orgType,
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

  void define(const char *fieldName, const DSType &orgType, HandleKind kind = HandleKind::VAR,
              HandleAttr attr = {}) {
    define(*this->orgCtx, fieldName, orgType, kind, attr);
  }

  static void define(AnalyzerContext &ctx, const char *fieldName, const DSType &type,
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
    auto *type = loadFromArchive(parent.getPool(), *archive);
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
    auto *newModType = loadFromArchive(this->newCtx->getPool(), *archive);
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
  auto ret1 = this->pool().createArrayType(this->pool().get(TYPE::GlobbingError));
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
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", "[[GlobbingError]]"));
}

TEST_F(ArchiveTest, map) {
  //
  auto ret1 = this->pool().createMapType(this->pool().get(TYPE::Signal),
                                         this->pool().get(TYPE::GlobbingError));
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
  std::vector<const DSType *> types;
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
  types = std::vector<const DSType *>();
  types.push_back(ret1.asOk());
  ret1 = this->pool().createTupleType(std::move(types));
  ASSERT_TRUE(ret1);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("bbb", "([Int : Int],)"));
}

TEST_F(ArchiveTest, option) {
  //
  auto ret1 = this->pool().createOptionType(this->pool().get(TYPE::UnwrappingError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("w1", "UnwrappingError?", HandleKind::TYPE_ALIAS));

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
  std::vector<const DSType *> types;
  auto ret1 = this->pool().createFuncType(this->pool().get(TYPE::Void), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("e1", type1, HandleKind::VAR, HandleAttr::READ_ONLY));

  //
  types = std::vector<const DSType *>();
  types.push_back(this->pool().getType("(Signal) -> Void"));
  types.push_back(&type1);
  ret1 = this->pool().createTupleType(std::vector<const DSType *>(types));
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
  std::vector<const DSType *> types;
  auto ret1 = this->pool().createFuncType(this->pool().get(TYPE::Void), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_NO_FATAL_FAILURE(this->define("AAA", type1, HandleKind::VAR, HandleAttr::READ_ONLY));

  //
  auto ret2 = this->pool().createOptionType(this->pool().get(TYPE::UnwrappingError));
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
                                              ctx.getPool().get(TYPE::GlobbingError));
      ASSERT_TRUE(ret1);
      auto &type1 = *ret1.asOk();
      ASSERT_NO_FATAL_FAILURE(define(ctx, "AAA", type1, HandleKind::ENV));
      ASSERT_NO_FATAL_FAILURE(
          define(ctx, "BBB", ctx.getPool().get(TYPE::TypeCastError), HandleKind::VAR));
    });
    ASSERT_EQ(3, toUnderlying(modType3.getModId()));
    ASSERT_EQ(1, modType3.getChildSize());
    ASSERT_TRUE(this->newPool().get(modType3.getChildAt(0).typeId()).isModType());

    auto ret1 = this->pool().createArrayType(this->pool().get(TYPE::GlobbingError));
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
  ASSERT_EQ(this->newPool().getType("[Signal : GlobbingError]")->typeId(), handle->getTypeId());
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
  ASSERT_EQ(this->newPool().getType("[GlobbingError]")->typeId(), handle->getTypeId());
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

  auto method = this->scope().lookupMethod(this->pool(), this->pool().get(TYPE::Int), "sum");
  ASSERT_TRUE(method);
  ASSERT_TRUE(method->isMethodHandle());
  ASSERT_EQ(1, method->getParamSize());
  ASSERT_EQ(this->pool().get(TYPE::Int), method->getParamTypeAt(0));
  ASSERT_EQ(this->pool().get(TYPE::Int), this->pool().get(method->getRecvTypeId()));
  ASSERT_EQ(this->pool().get(TYPE::Int), method->getReturnType());
  ASSERT_EQ("target", method->getPackedParamNames().toString());

  method = this->scope().lookupMethod(this->pool(), this->pool().get(TYPE::Int), "_value");
  ASSERT_FALSE(method);
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
          e.setAttr(ArgEntryAttr::REQUIRE);
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
          e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REMAIN | ArgEntryAttr::REQUIRE);
        });

    auto &recordType = createRecordType(ctx.getPool(), "type1", std::move(builder), ctx.getModId(),
                                        CLIRecordType::Attr::VERBOSE);
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
  ASSERT_EQ(ArgEntryAttr::REQUIRE, entries[2].getAttr());
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
  ASSERT_EQ(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REMAIN | ArgEntryAttr::REQUIRE,
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
    std::vector<std::pair<ImportedModKind, ModuleArchivePtr>> deps;
    build(deps, std::forward<Args>(args)...);
    auto src = this->srcMan.update(path, 0, "");
    auto archive = std::make_shared<ModuleArchive>(src->getSrcId(), src->getVersion(), ModAttr{},
                                                   std::move(handles), std::move(deps));
    this->archives.add(archive);
    return archive;
  }

private:
  template <typename... Args>
  static void build(std::vector<std::pair<ImportedModKind, ModuleArchivePtr>> &deps,
                    const ModuleArchivePtr &archive, Args &&...args) {
    deps.emplace_back(ImportedModKind::GLOBAL, archive);
    build(deps, std::forward<Args>(args)...);
  }

  static void build(std::vector<std::pair<ImportedModKind, ModuleArchivePtr>> &) {}
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}