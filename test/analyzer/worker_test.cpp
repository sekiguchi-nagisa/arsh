#include "../test_common.h"

#include "analyzer_worker.h"
#include "worker.h"

using namespace arsh;
using namespace arsh::lsp;

TEST(WorkerTest, base) {
  SingleBackgroundWorker worker;
  auto ret1 = worker.addTask([] {
    std::string value;
    for (unsigned int i = 0; i < 10; i++) {
      if (!value.empty()) {
        value += "_";
      }
      value += std::to_string(i);
    }
    return value;
  });

  auto ret2 = worker.addTask(
      [](unsigned int offset) {
        std::string value;
        for (unsigned int i = offset; i < 20; i++) {
          if (!value.empty()) {
            value += ".";
          }
          value += std::to_string(i);
        }
        return value;
      },
      10);

  ASSERT_EQ("10.11.12.13.14.15.16.17.18.19", ret2.get());
  ASSERT_EQ("0_1_2_3_4_5_6_7_8_9", ret1.get());
}

TEST(AnalyzerWorkerStateTest, base) {
  auto state1 = AnalyzerWorker::State::create("", 0);
  auto ret = state1.updateSource("/dummy1", 0, "dummy1");
  ASSERT_EQ(ModId{1}, ret.first->getSrcId());
  ASSERT_EQ("/dummy1", ret.first->getPath());
  ASSERT_EQ("dummy1\n", ret.first->getContent());
  ASSERT_TRUE(ret.second);
  ASSERT_TRUE(state1.modifiedSrcIds.find(ModId{1}) != state1.modifiedSrcIds.end());
  ASSERT_EQ(1, state1.modifiedSrcIds.size());

  ret = state1.updateSource("/dummy2", 0, "dummy2");
  ASSERT_EQ(ModId{2}, ret.first->getSrcId());
  ASSERT_EQ("/dummy2", ret.first->getPath());
  ASSERT_EQ("dummy2\n", ret.first->getContent());
  ASSERT_TRUE(ret.second);
  ASSERT_TRUE(state1.modifiedSrcIds.find(ModId{2}) != state1.modifiedSrcIds.end());
  ASSERT_EQ(2, state1.modifiedSrcIds.size());

  // update
  ret = state1.updateSource("/dummy2", 1, "dummy2-1");
  ASSERT_EQ(ModId{2}, ret.first->getSrcId());
  ASSERT_EQ("/dummy2", ret.first->getPath());
  ASSERT_EQ("dummy2-1\n", ret.first->getContent());
  ASSERT_TRUE(ret.second);
  ASSERT_TRUE(state1.modifiedSrcIds.find(ModId{2}) != state1.modifiedSrcIds.end());
  ASSERT_EQ(2, state1.modifiedSrcIds.size());

  // update but, not change hash
  state1.modifiedSrcIds.clear();
  ret = state1.updateSource("/dummy2", 2, "dummy2-1");
  ASSERT_EQ(ModId{2}, ret.first->getSrcId());
  ASSERT_EQ("/dummy2", ret.first->getPath());
  ASSERT_EQ("dummy2-1\n", ret.first->getContent());
  ASSERT_FALSE(ret.second);
  ASSERT_EQ(0, state1.modifiedSrcIds.size());
}

TEST(AnalyzerWorkerStateTest, merge) {
  auto state1 = AnalyzerWorker::State::create("", 0);
  auto state2 = state1.deepCopy();

  const struct {
    const char *path;
    const char *content;
    ModId modId;
  } data1[] = {{"/dummy1", "dummy1", ModId{1}},
               {"/dummy2", "dummy2", ModId{2}},
               {"/dummy3", "dummy3", ModId{3}}};
  for (auto &e : data1) {
    auto src = state1.updateSource(e.path, 1, e.content);
    ASSERT_TRUE(src.first);
    ASSERT_TRUE(src.second);
    ASSERT_EQ(e.modId, src.first->getSrcId());
  }

  const struct {
    const char *path;
    const char *content;
    ModId modId;
  } data2[] = {{"/dummy2", "dummy2!", ModId{1}},
               {"/dummy3", "dummy3!", ModId{2}},
               {"/dummy4", "dummy4!", ModId{3}},
               {"/dummy5", "dummy5!", ModId{4}}};
  for (auto &e : data2) {
    auto src = state2.updateSource(e.path, 1, e.content);
    ASSERT_TRUE(src.first);
    ASSERT_TRUE(src.second);
    ASSERT_EQ(e.modId, src.first->getSrcId());
  }

  state1.modifiedSrcIds.erase(ModId{2});
  state2.modifiedSrcIds.erase(ModId{1});
  state2.modifiedSrcIds.erase(ModId{2});
  state2.modifiedSrcIds.erase(ModId{4});
  ASSERT_EQ(1, state2.modifiedSrcIds.size());
  ASSERT_EQ(2, state1.modifiedSrcIds.size());

  // merge
  state2.mergeSources(state1);
  ASSERT_EQ(3, state2.modifiedSrcIds.size());
  ASSERT_FALSE(state2.modifiedSrcIds.find(ModId{1}) != state2.modifiedSrcIds.end());
  ASSERT_TRUE(state2.modifiedSrcIds.find(ModId{2}) != state2.modifiedSrcIds.end());
  ASSERT_TRUE(state2.modifiedSrcIds.find(ModId{3}) != state2.modifiedSrcIds.end());
  ASSERT_FALSE(state2.modifiedSrcIds.find(ModId{4}) != state2.modifiedSrcIds.end());
  ASSERT_TRUE(state2.modifiedSrcIds.find(ModId{5}) != state2.modifiedSrcIds.end());
  ASSERT_FALSE(state2.modifiedSrcIds.find(ModId{6}) != state2.modifiedSrcIds.end());
  auto src = state2.srcMan->findById(ModId{1});
  ASSERT_EQ("/dummy2", src->getPath());
  ASSERT_EQ("dummy2!\n", src->getContent()); // only update modified source
  src = state2.srcMan->findById(ModId{2});
  ASSERT_EQ("/dummy3", src->getPath());
  ASSERT_EQ("dummy3\n", src->getContent()); // overwrite
  src = state2.srcMan->findById(ModId{3});
  ASSERT_EQ("/dummy4", src->getPath());
  ASSERT_EQ("dummy4!\n", src->getContent());
  src = state2.srcMan->findById(ModId{4});
  ASSERT_EQ("/dummy5", src->getPath());
  ASSERT_EQ("dummy5!\n", src->getContent());
  src = state2.srcMan->findById(ModId{5});
  ASSERT_EQ("/dummy1", src->getPath());
  ASSERT_EQ("dummy1\n", src->getContent()); // add
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}