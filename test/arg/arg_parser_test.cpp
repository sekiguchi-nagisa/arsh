#include "gtest/gtest.h"

#include <vm.h>
#include <ydsh/ydsh.h>

using namespace ydsh;

class ArgParserTest : public ::testing::Test {
protected:
  DSState *state{nullptr};

public:
  void SetUp() override { this->state = DSState_create(); }

  void TearDown() override { DSState_delete(&this->state); }
};

template <typename... T>
static ObjPtr<ArrayObject> createArgs(T &&...args) {
  std::vector<DSValue> values = {DSValue::createStr(args)...};
  auto v = DSValue::create<ArrayObject>(toUnderlying(TYPE::StringArray), std::move(values));
  return toObjPtr<ArrayObject>(v);
}

static std::string toStringAt(const ArrayObject &obj, size_t index) {
  return obj.getValues()[index].asStrRef().toString();
}

TEST_F(ArgParserTest, base) {
  auto args = createArgs("-A", "--");
  ASSERT_EQ(2, args->size());
  ASSERT_EQ("-A", toStringAt(*args, 0));
  ASSERT_EQ("--", toStringAt(*args, 1));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}