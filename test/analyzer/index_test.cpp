
#include "gtest/gtest.h"

#include "analyzer.h"

using namespace ydsh::lsp;

struct IndexBuilder {
  unsigned short id;
  std::vector<IndexBuilder> children;

  ModuleIndexPtr build() const {
    std::unique_ptr<ydsh::TypePool> pool;
    std::vector<std::unique_ptr<ydsh::Node>> nodes;
    ModuleArchive archive({});
    std::vector<std::pair<bool, ModuleIndexPtr>> deps;
    for (auto &e : this->children) {
      deps.emplace_back(true, e.build());
    }
    return ModuleIndex::create(this->id, 0, std::move(pool), std::move(nodes), std::move(archive),
                               std::move(deps));
  }
};

template <size_t N, typename... T>
IndexBuilder tree(T &&...args) {
  return IndexBuilder{
      .id = N,
      .children = {std::forward<T>(args)...},
  };
}

TEST(IndexTest, deps1) {
  auto t = tree<1>(tree<2>(tree<5>(tree<7>()), tree<6>()), tree<3>(tree<4>()));
  auto index = t.build();
  ASSERT_EQ(2, index->getImportedIndexes().size());
  ASSERT_EQ(2, index->getImportedIndexes()[0].second->getModId());
  ASSERT_EQ(3, index->getImportedIndexes()[1].second->getModId());
  auto deps = index->getDepsByTopologicalOrder();
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(7, deps[0]->getModId());
  ASSERT_EQ(5, deps[1]->getModId());
  ASSERT_EQ(6, deps[2]->getModId());
  ASSERT_EQ(2, deps[3]->getModId());
  ASSERT_EQ(4, deps[4]->getModId());
  ASSERT_EQ(3, deps[5]->getModId());
}

TEST(IndexTest, deps2) {
  auto t1 = tree<1>(tree<2>(), tree<3>(tree<4>()));
  auto t2 = tree<5>(tree<2>(), t1, tree<6>(tree<4>()), tree<3>(tree<4>()));
  auto t3 = tree<7>(t2, t1);
  auto index = t3.build();
  ASSERT_EQ(2, index->getImportedIndexes().size());
  ASSERT_EQ(5, index->getImportedIndexes()[0].second->getModId());
  ASSERT_EQ(1, index->getImportedIndexes()[1].second->getModId());
  auto deps = index->getDepsByTopologicalOrder();
  ASSERT_EQ(6, deps.size());
  ASSERT_EQ(2, deps[0]->getModId());
  ASSERT_EQ(4, deps[1]->getModId());
  ASSERT_EQ(3, deps[2]->getModId());
  ASSERT_EQ(1, deps[3]->getModId());
  ASSERT_EQ(6, deps[4]->getModId());
  ASSERT_EQ(5, deps[5]->getModId());
}

TEST(IndexTest, deps3) {
  auto t = tree<1>();
  auto index = t.build();
  ASSERT_EQ(0, index->getImportedIndexes().size());
  auto deps = index->getDepsByTopologicalOrder();
  ASSERT_EQ(0, deps.size());
}

struct Builder {
  SourceManager &srcMan;
  IndexMap &indexMap;

  template <typename... Args>
  ModuleIndexPtr operator()(const char *path, Args &&...args) {
    std::unique_ptr<ydsh::TypePool> pool;
    std::vector<std::unique_ptr<ydsh::Node>> nodes;
    ModuleArchive archive({});
    std::vector<std::pair<bool, ModuleIndexPtr>> deps;
    build(deps, std::forward<Args>(args)...);
    auto src = this->srcMan.update(path, 0, "");
    auto index = ModuleIndex::create(src->getSrcId(), src->getVersion(), std::move(pool),
                                     std::move(nodes), std::move(archive), std::move(deps));
    this->indexMap.add(*src, index);
    return index;
  }

private:
  template <typename... Args>
  static void build(std::vector<std::pair<bool, ModuleIndexPtr>> &deps, const ModuleIndexPtr &index,
                    Args &&...args) {
    deps.emplace_back(true, index);
    build(deps, std::forward<Args>(args)...);
  }

  static void build(std::vector<std::pair<bool, ModuleIndexPtr>> &) {}
};

TEST(IndexTest, revert1) {
  SourceManager srcMan;
  IndexMap indexMap;
  Builder builder = {
      .srcMan = srcMan,
      .indexMap = indexMap,
  };

  auto t1 = builder("/aaa");
  auto t3 = builder("/ccc", t1);
  auto t4 = builder("/ddd");
  auto t2 = builder("/bbb", t3, t4);
  ASSERT_EQ(4, indexMap.size());

  indexMap.revert({indexMap.find(*srcMan.find("/ddd"))->getModId()});
  ASSERT_EQ(2, indexMap.size());
  ASSERT_EQ(1, indexMap.find(*srcMan.find("/aaa"))->getModId());
  ASSERT_EQ(2, indexMap.find(*srcMan.find("/ccc"))->getModId());

  indexMap.revert({indexMap.find(*srcMan.find("/aaa"))->getModId()});
  ASSERT_EQ(0, indexMap.size());
}

TEST(IndexTest, revert2) {
  SourceManager srcMan;
  IndexMap indexMap;
  Builder builder = {
      .srcMan = srcMan,
      .indexMap = indexMap,
  };

  auto t1 = builder("/aaa");
  auto t3 = builder("/ccc", t1);
  auto t4 = builder("/ddd");
  auto t2 = builder("/bbb", t3, t4);
  auto t5 = builder("/eee");
  ASSERT_EQ(5, indexMap.size());

  indexMap.revertIfUnused(t1->getModId());
  ASSERT_EQ(5, indexMap.size());

  indexMap.revertIfUnused(t4->getModId());
  ASSERT_EQ(5, indexMap.size());

  indexMap.revertIfUnused(t5->getModId());
  ASSERT_EQ(4, indexMap.size());
  ASSERT_EQ(nullptr, indexMap.find(*srcMan.find("/eee")));

  indexMap.revert({t1->getModId(), t4->getModId()});
  ASSERT_EQ(0, indexMap.size());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}