
#include "gtest/gtest.h"

#include "analyzer.h"
#include "archive.h"

using namespace ydsh::lsp;
using namespace ydsh;

TEST(SourceTest, base) {
  SourceManager srcMan;
  auto src = srcMan.findById(111);
  ASSERT_FALSE(src);
  src = srcMan.find("ssss");
  ASSERT_FALSE(src);

  src = srcMan.update("/dummy1", 10, "hello");
  ASSERT_TRUE(src);
  ASSERT_EQ(10, src->getVersion());
  ASSERT_STREQ("/dummy1", src->getPath());
  ASSERT_EQ("hello\n", src->getContent());
  ASSERT_EQ(1, src->getSrcId());

  src = srcMan.update("/dummy1", 12, "world");
  ASSERT_TRUE(src);
  ASSERT_EQ(12, src->getVersion());
  ASSERT_STREQ("/dummy1", src->getPath());
  ASSERT_EQ("world\n", src->getContent());
  ASSERT_EQ(1, src->getSrcId());

  src = srcMan.update("/dummy2", 1, "");
  ASSERT_TRUE(src);
  ASSERT_EQ(1, src->getVersion());
  ASSERT_STREQ("/dummy2", src->getPath());
  ASSERT_EQ("\n", src->getContent());
  ASSERT_EQ(2, src->getSrcId());
}

struct ArchiveBuilder {
  unsigned short id;
  std::vector<ArchiveBuilder> children;

  ModuleArchivePtr build() const {
    std::vector<Archive> handles;
    std::vector<std::pair<bool, ModuleArchivePtr>> imported;
    for (auto &e : this->children) {
      imported.emplace_back(true, e.build());
    }
    return std::make_shared<ModuleArchive>(this->id, 0, std::move(handles), std::move(imported));
  }
};

template <size_t N, typename... T>
ArchiveBuilder tree(T &&...args) {
  return ArchiveBuilder{
      .id = N,
      .children = {std::forward<T>(args)...},
  };
}

TEST(DependentTest, deps1) {
  auto t = tree<1>(tree<2>(tree<5>(tree<7>()), tree<6>()), tree<3>(tree<4>()));
  auto archive = t.build();
  ASSERT_EQ(2, archive->getImported().size());
  ASSERT_EQ(2, archive->getImported()[0].second->getModID());
  ASSERT_EQ(3, archive->getImported()[1].second->getModID());
  auto deps = archive->getDepsByTopologicalOrder();
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(7, deps[0]->getModID());
  ASSERT_EQ(5, deps[1]->getModID());
  ASSERT_EQ(6, deps[2]->getModID());
  ASSERT_EQ(2, deps[3]->getModID());
  ASSERT_EQ(4, deps[4]->getModID());
  ASSERT_EQ(3, deps[5]->getModID());
}

TEST(DependentTest, deps2) {
  auto t1 = tree<1>(tree<2>(), tree<3>(tree<4>()));
  auto t2 = tree<5>(tree<2>(), t1, tree<6>(tree<4>()), tree<3>(tree<4>()));
  auto t3 = tree<7>(t2, t1);
  auto archive = t3.build();
  ASSERT_EQ(2, archive->getImported().size());
  ASSERT_EQ(5, archive->getImported()[0].second->getModID());
  ASSERT_EQ(1, archive->getImported()[1].second->getModID());
  auto deps = archive->getDepsByTopologicalOrder();
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(2, deps[0]->getModID());
  ASSERT_EQ(4, deps[1]->getModID());
  ASSERT_EQ(3, deps[2]->getModID());
  ASSERT_EQ(1, deps[3]->getModID());
  ASSERT_EQ(6, deps[4]->getModID());
  ASSERT_EQ(5, deps[5]->getModID());
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
  SourceManager srcMan;
  ModuleArchives archives;
  AnalyzerContextPtr orgCtx;
  AnalyzerContextPtr newCtx;

protected:
  const unsigned int builtinIdOffset;

  static AnalyzerContextPtr newctx(SourceManager &srcMan, ModuleArchives &archives) {
    std::string path = "/dummy_";
    path += std::to_string(archives.size() + 1);
    auto src = srcMan.update(path, 0, "");
    archives.reserve(src->getSrcId());
    return std::make_unique<AnalyzerContext>(*src);
  }

  AnalyzerContextPtr newctx() { return newctx(this->srcMan, this->archives); }

  static auto toSorted(const std::unordered_map<std::string, FieldHandle> &handleMap) {
    using Entry = std::pair<std::string, FieldHandle>;
    std::vector<Entry> ret;
    for (auto &e : handleMap) {
      ret.emplace_back(e.first, e.second);
    }
    std::sort(ret.begin(), ret.end(),
              [](const Entry &x, const Entry &y) { return x.first < y.first; });
    return ret;
  }

public:
  ArchiveTest()
      : orgCtx(newctx(this->srcMan, this->archives)), newCtx(newctx(this->srcMan, this->archives)),
        builtinIdOffset(this->orgCtx->getPool().getDiscardPoint().typeIdOffset) {}

  TypePool &pool() { return this->orgCtx->getPool(); }

  const TypePool &newPool() const { return this->newCtx->getPool(); }

  void defineAndArchive(const char *fieldName, const char *typeName, FieldAttribute attr = {}) {
    ASSERT_TRUE(typeName);
    auto ret = this->orgCtx->getPool().getType(typeName);
    ASSERT_TRUE(ret);
    this->defineAndArchive(fieldName, *ret.asOk(), attr);
  }

  void defineAndArchive(const char *fieldName, const DSType &orgType, FieldAttribute attr = {}) {
    ASSERT_TRUE(fieldName);
    ASSERT_TRUE(orgType.typeId() < this->pool().getDiscardPoint().typeIdOffset);
    ASSERT_EQ(orgType, this->orgCtx->getPool().get(orgType.typeId()));

    // define handle
    auto ret1 = this->orgCtx->getScope()->defineHandle(fieldName, orgType, attr);
    ASSERT_TRUE(ret1);
    auto &orgHandle = *ret1.asOk();

    // serialize
    auto archive =
        Archive::pack(this->orgCtx->getPool(), this->builtinIdOffset, fieldName, orgHandle);
    ASSERT_EQ(fieldName, archive.getName());
    ASSERT_FALSE(archive.getData().empty());

    // deserialize
    auto ret2 = archive.unpack(this->newCtx->getPool()); // deserialize in another context
    ASSERT_TRUE(ret2.hasValue());
    auto &newHandle = ret2.unwrap();

    // compare 2 handles
    ASSERT_EQ(orgHandle.getModID(), newHandle.getModID());
    ASSERT_EQ(orgHandle.getIndex(), newHandle.getIndex());
    ASSERT_EQ(orgHandle.getCommitID(), newHandle.getCommitID());
    ASSERT_EQ(toString(orgHandle.attr()), toString(newHandle.attr()));
    ASSERT_TRUE(newHandle.getTypeID() <= this->newCtx->getPool().getDiscardPoint().typeIdOffset);
    auto &newType = this->newCtx->getPool().get(newHandle.getTypeID());
    ASSERT_EQ(orgType.getNameRef(), newType.getNameRef());
  }

  void define(const char *fieldName, const DSType &orgType, FieldAttribute attr = {}) {
    define(*this->orgCtx, fieldName, orgType, attr);
  }

  static void define(AnalyzerContext &ctx, const char *fieldName, const DSType &type,
                     FieldAttribute attr = {}) {
    ASSERT_TRUE(fieldName);
    ASSERT_TRUE(type.typeId() < ctx.getPool().getDiscardPoint().typeIdOffset);
    ASSERT_EQ(type, ctx.getPool().get(type.typeId()));

    auto ret = ctx.getScope()->defineHandle(fieldName, type, attr);
    ASSERT_TRUE(ret);
    auto *handle = ret.asOk();
    ASSERT_EQ(type.typeId(), handle->getTypeID());
    auto orgAttr = attr;
    unsetFlag(orgAttr, FieldAttribute::GLOBAL);
    auto newAttr = handle->attr();
    unsetFlag(newAttr, FieldAttribute::GLOBAL);
    ASSERT_EQ(toString(orgAttr), toString(newAttr));
  }

  template <typename Func>
  const ModType &loadMod(bool global, Func func) {
    return this->loadModAt(*this->orgCtx, global, std::move(func));
  }

  template <typename Func>
  const ModType &loadModAt(AnalyzerContext &parent, bool global, Func func) {
    auto ctx = newctx(this->srcMan, this->archives);
    func(*ctx);
    auto archive = std::move(*ctx).buildArchive(this->archives);
    auto *type = loadFromArchive(parent.getPool(), *archive);
    assert(type);
    parent.getScope()->importForeignHandles(*type, global);
    return *type;
  }

  void archiveMod(std::vector<std::string> &&expected = {}) {
    // serialize
    auto poolPtr = this->orgCtx->getPoolPtr();
    auto archive = std::move(*this->orgCtx).buildArchive(this->archives);
    ASSERT_TRUE(archive);
    unsigned id = archive->getModID();
    auto ret = poolPtr->getModTypeById(id);
    ASSERT_TRUE(ret);
    auto *orgModType = static_cast<const ModType *>(ret.asOk());

    // deserialize
    auto *newModType = loadFromArchive(this->newCtx->getPool(), *archive);
    ASSERT_TRUE(newModType);

    // compare
    ASSERT_EQ(orgModType->getModID(), newModType->getModID());
    ASSERT_EQ(orgModType->getChildSize(), newModType->getChildSize());
    for (unsigned int i = 0; i < orgModType->getChildSize(); i++) {
      auto oe = orgModType->getChildAt(i);
      auto ne = newModType->getChildAt(i);
      ASSERT_EQ(oe.isGlobal(), ne.isGlobal());
      auto &om = poolPtr->get(oe.typeId());
      ASSERT_TRUE(om.isModType());
      auto &nm = this->newCtx->getPool().get(ne.typeId());
      ASSERT_TRUE(nm.isModType());
      ASSERT_EQ(static_cast<const ModType &>(om).getModID(),
                static_cast<const ModType &>(nm).getModID());
      ASSERT_EQ(om.getNameRef(), nm.getNameRef());
    }

    ASSERT_EQ(orgModType->getHandleMap().size(), newModType->getHandleMap().size());
    ASSERT_EQ(poolPtr->getDiscardPoint().typeIdOffset,
              this->newCtx->getPool().getDiscardPoint().typeIdOffset);

    auto orgHandles = toSorted(orgModType->getHandleMap());
    auto newHandles = toSorted(newModType->getHandleMap());
    ASSERT_EQ(orgHandles.size(), newHandles.size());
    for (unsigned int i = 0; i < orgHandles.size(); i++) {
      auto &orgEntry = orgHandles[i];
      auto &newEntry = newHandles[i];
      ASSERT_EQ(orgEntry.first, newEntry.first);
      ASSERT_EQ(orgEntry.second.getModID(), newEntry.second.getModID());
      ASSERT_EQ(toString(orgEntry.second.attr()), toString(newEntry.second.attr()));
    }

    // check specified names
    for (auto &e : expected) {
      auto *handle = newModType->lookup(e);
      ASSERT_TRUE(handle);
    }
  }
};

TEST_F(ArchiveTest, base) {
  auto ctx = this->newctx();
  auto *handle = ctx->getScope()->find("COMP_HOOK");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(hasFlag(handle->attr(), FieldAttribute::GLOBAL));
  ASSERT_EQ(0, handle->getModID());
  handle = ctx->getScope()->find("TRUE");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(hasFlag(handle->attr(), FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY));
  ASSERT_EQ(0, handle->getModID());
  ASSERT_TRUE(ctx->getTypeIdOffset() <= UINT8_MAX);
}

TEST_F(ArchiveTest, predefined) {
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("a", "[String]"));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("b", "String", FieldAttribute::ENV));
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("c", "((String, [String]) -> Void)!", FieldAttribute{}));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("d", "(Signal) -> Void", FieldAttribute::RANDOM));
}

TEST_F(ArchiveTest, array) {
  //
  auto ret1 = this->pool().createArrayType(this->pool().get(TYPE::GlobbingError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa", type1));
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("bbb", type1, FieldAttribute::SECONDS | FieldAttribute::ALIAS));

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
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa", type1, FieldAttribute::READ_ONLY));
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("bbb", type1, FieldAttribute::RANDOM | FieldAttribute::ALIAS));

  //
  ret1 =
      this->pool().createMapType(this->pool().get(TYPE::String), this->pool().get(TYPE::Boolean));
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
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa12", type1, FieldAttribute::READ_ONLY));

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
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("w1", "UnwrappingError!", FieldAttribute::ALIAS));

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
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("e1", type1, FieldAttribute::READ_ONLY));

  //
  types = std::vector<const DSType *>();
  types.push_back(this->pool().getType("(Signal) -> Void").asOk());
  types.push_back(&type1);
  ret1 = this->pool().createTupleType(std::vector<const DSType *>(types));
  ASSERT_TRUE(ret1);
  types.push_back(ret1.asOk());
  ret1 = this->pool().createFuncType(this->pool().get(TYPE::Nothing), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", type2, FieldAttribute::ALIAS));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("DDD", type2, FieldAttribute{}));
}

TEST_F(ArchiveTest, mod1) { ASSERT_NO_FATAL_FAILURE(this->archiveMod()); }

TEST_F(ArchiveTest, mod2) {
  //
  std::vector<const DSType *> types;
  auto ret1 = this->pool().createFuncType(this->pool().get(TYPE::Void), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_NO_FATAL_FAILURE(this->define("AAA", type1, FieldAttribute::READ_ONLY));

  //
  auto ret2 = this->pool().createOptionType(this->pool().get(TYPE::UnwrappingError));
  ASSERT_TRUE(ret2);
  auto &type2 = *ret2.asOk();
  ASSERT_NO_FATAL_FAILURE(
      this->define("a12345", type2, FieldAttribute::ALIAS | FieldAttribute::READ_ONLY));

  ASSERT_NO_FATAL_FAILURE(this->archiveMod({"AAA", "a12345"}));
}

TEST_F(ArchiveTest, mod3) {
  {
    auto &modType3 = this->loadMod(false, [](AnalyzerContext &ctx) {
      auto ret1 = ctx.getPool().createMapType(ctx.getPool().get(TYPE::Signal),
                                              ctx.getPool().get(TYPE::GlobbingError));
      ASSERT_TRUE(ret1);
      auto &type1 = *ret1.asOk();
      ASSERT_NO_FATAL_FAILURE(define(ctx, "AAA", type1, FieldAttribute::ENV));
      ASSERT_NO_FATAL_FAILURE(
          define(ctx, "BBB", ctx.getPool().get(TYPE::TypeCastError), FieldAttribute::RANDOM));
    });
    ASSERT_EQ(3, modType3.getModID());
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
  auto ret1 = this->newPool().getModTypeById(3);
  ASSERT_TRUE(ret1);
  auto &modType3 = static_cast<const ModType &>(*ret1.asOk());
  auto *handle = modType3.lookup("AAA");
  ASSERT_TRUE(handle);
  ASSERT_EQ(this->newPool().getType("[Signal : GlobbingError]").asOk()->typeId(),
            handle->getTypeID());
  ASSERT_EQ(toString(FieldAttribute::ENV | FieldAttribute::GLOBAL), toString(handle->attr()));
  ASSERT_EQ(modType3.getModID(), handle->getModID());

  handle = modType3.lookup("BBB");
  ASSERT_TRUE(handle);
  ASSERT_EQ(this->newPool().getType("TypeCastError").asOk()->typeId(), handle->getTypeID());
  ASSERT_EQ(toString(FieldAttribute::GLOBAL | FieldAttribute::RANDOM), toString(handle->attr()));
  ASSERT_EQ(modType3.getModID(), handle->getModID());

  //
  ret1 = this->newPool().getModTypeById(1);
  ASSERT_TRUE(ret1);
  auto &modType1 = static_cast<const ModType &>(*ret1.asOk());
  ASSERT_EQ(1, modType1.getModID());
  ASSERT_EQ(2, modType1.getChildSize());
  ASSERT_TRUE(modType1.getChildAt(0).isGlobal());
  ASSERT_FALSE(modType1.getChildAt(1).isGlobal());
  ASSERT_EQ(modType3.typeId(), modType1.getChildAt(1).typeId());

  handle = modType1.lookup("AAA");
  ASSERT_FALSE(handle);

  handle = modType1.lookup("aaa");
  ASSERT_TRUE(handle);
  ASSERT_EQ(this->newPool().getType("[GlobbingError]").asOk()->typeId(), handle->getTypeID());
  ASSERT_EQ(toString(FieldAttribute::GLOBAL), toString(handle->attr()));
  ASSERT_EQ(1, handle->getModID());
}

TEST_F(ArchiveTest, mod4) {
  {
    auto &modType3 = this->loadMod(false, [&](AnalyzerContext &ctx1) {
      auto &modType4 = this->loadModAt(ctx1, true, [](AnalyzerContext &ctx2) {
        auto ret = ctx2.getPool().createArrayType(ctx2.getPool().get(TYPE::Boolean));
        ASSERT_TRUE(ret);
        ctx2.getScope()->defineTypeAlias(ctx2.getPool(), "BoolArray", *ret.asOk());
        ASSERT_NO_FATAL_FAILURE(define(ctx2, "AAA", *ret.asOk(), FieldAttribute::READ_ONLY));
      });
      ASSERT_EQ(4, modType4.getModID());

      auto ret = ctx1.getPool().createTupleType(
          {&ctx1.getPool().get(TYPE::IllegalAccessError), &modType4});
      ASSERT_TRUE(ret);
      ASSERT_NO_FATAL_FAILURE(define(ctx1, "BBB", *ret.asOk(), FieldAttribute::READ_ONLY));
    });
    ASSERT_NO_FATAL_FAILURE(define("CCC", modType3, FieldAttribute::READ_ONLY));

    ASSERT_EQ(3, modType3.getModID());
    ASSERT_EQ(2, modType3.getChildSize());
  }
  ASSERT_NO_FATAL_FAILURE(this->archiveMod({"CCC"}));

  auto ret = this->newPool().getModTypeById(3);
  ASSERT_TRUE(ret);
  auto &modType3 = static_cast<const ModType &>(*ret.asOk());
  ASSERT_EQ(3, modType3.getModID());
  ASSERT_EQ(2, modType3.getChildSize());
  auto *handle = modType3.lookup("BBB");
  ASSERT_TRUE(handle);
  ASSERT_EQ(3, handle->getModID());
  auto &type1 = this->newPool().get(handle->getTypeID());
  ASSERT_TRUE(type1.isTupleType());
  auto &tuple = static_cast<const TupleType &>(type1);
  ASSERT_EQ(2, tuple.getFieldSize());
  ASSERT_EQ(this->newPool().get(TYPE::IllegalAccessError),
            tuple.getFieldTypeAt(this->newPool(), 0));
  ASSERT_TRUE(tuple.getFieldTypeAt(this->newPool(), 1).isModType());
  ASSERT_EQ(4, static_cast<const ModType &>(tuple.getFieldTypeAt(this->newPool(), 1)).getModID());

  handle = modType3.lookup("AAA");
  ASSERT_FALSE(handle);
  handle = modType3.lookupVisibleSymbolAtModule(this->newPool(), "AAA");
  ASSERT_TRUE(handle);
  ASSERT_EQ(4, handle->getModID());
  ret = this->newPool().getType("[Boolean]");
  ASSERT_TRUE(ret);
  ASSERT_EQ(ret.asOk()->typeId(), handle->getTypeID());
}

struct Builder {
  SourceManager &srcMan;
  ModuleArchives &archives;

  template <typename... Args>
  ModuleArchivePtr operator()(const char *path, Args &&...args) {
    std::vector<Archive> handles;
    std::vector<std::pair<bool, ModuleArchivePtr>> deps;
    build(deps, std::forward<Args>(args)...);
    auto src = this->srcMan.update(path, 0, "");
    auto archive = std::make_shared<ModuleArchive>(src->getSrcId(), src->getVersion(),
                                                   std::move(handles), std::move(deps));
    this->archives.add(archive);
    return archive;
  }

private:
  template <typename... Args>
  static void build(std::vector<std::pair<bool, ModuleArchivePtr>> &deps,
                    const ModuleArchivePtr &archive, Args &&...args) {
    deps.emplace_back(true, archive);
    build(deps, std::forward<Args>(args)...);
  }

  static void build(std::vector<std::pair<bool, ModuleArchivePtr>> &) {}
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
  ASSERT_EQ(4, archives.size());

  archives.revert({archives.find(srcMan.find("/ddd")->getSrcId())->getModID()});
  ASSERT_EQ(2, archives.size());
  ASSERT_EQ(1, archives.find(srcMan.find("/aaa")->getSrcId())->getModID());
  ASSERT_EQ(2, archives.find(srcMan.find("/ccc")->getSrcId())->getModID());

  archives.revert({archives.find(srcMan.find("/aaa")->getSrcId())->getModID()});
  ASSERT_EQ(0, archives.size());
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
  ASSERT_EQ(5, archives.size());

  archives.revertIfUnused(t1->getModID());
  ASSERT_EQ(5, archives.size());

  archives.revertIfUnused(t4->getModID());
  ASSERT_EQ(5, archives.size());

  archives.revertIfUnused(t5->getModID());
  ASSERT_EQ(4, archives.size());
  ASSERT_EQ(nullptr, archives.find(srcMan.find("/eee")->getSrcId()));

  archives.revert({t1->getModID(), t4->getModID()});
  ASSERT_EQ(0, archives.size());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}