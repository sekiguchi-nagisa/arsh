#include "gtest/gtest.h"

#include <misc/resource.hpp>

using namespace arsh;

struct AAAOp;

class AAAImpl {
private:
  long count{0};

  friend AAAOp;

public:
  virtual ~AAAImpl() = default;

  virtual const char *name() const { return "AAA"; }
};

struct BBBImpl : public AAAImpl {
  ~BBBImpl() override = default;

  const char *name() const override { return "BBB"; }
};

struct AAAOp {
  static long useCount(const AAAImpl *ptr) noexcept { return ptr->count; }

  static void increase(AAAImpl *ptr) noexcept {
    if (ptr != nullptr) {
      ptr->count++;
    }
  }

  static void decrease(AAAImpl *ptr) noexcept {
    if (ptr != nullptr && --ptr->count == 0) {
      delete ptr;
    }
  }
};

using AAA = IntrusivePtr<AAAImpl, AAAOp>;
using BBB = IntrusivePtr<BBBImpl, AAAOp>;

TEST(intrusive, base) {
  static_assert(sizeof(AAA) == sizeof(long));
  static_assert(sizeof(BBB) == sizeof(long));

  auto a = AAA::create();
  ASSERT_STREQ("AAA", a->name());
  auto b = BBB::create();
  ASSERT_STREQ("BBB", b->name());

  a = b;
  ASSERT_STREQ("BBB", a->name());
  ASSERT_EQ(2, a.useCount());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
