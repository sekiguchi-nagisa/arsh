#include "gtest/gtest.h"

#include <sstream>

#include <misc/resource.hpp>

using namespace ydsh;

TEST(resource, base) {
    std::stringstream s;

    {
        auto r = makeScopedResource(
                12, [&s](unsigned int i) {
                    s << "delete" << i;
                });
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12", s.str()));
}

TEST(resource, move1) {
    std::stringstream s;

    {
        auto r = makeScopedResource(
                12, [&s](unsigned int i) {
                    s << "delete" << i;
                });
        auto r2(std::move(r));
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12", s.str()));
}

TEST(resource, move2) {
    std::stringstream s;

    {
        auto r = makeScopedResource(
                12, [&s](unsigned int i) {
                    s << "delete" << i;
                });
        auto r2(std::move(r));
        auto r3(std::move(r));
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12", s.str()));
}

TEST(resource, moveAssign) {
    std::stringstream s;
     struct Deleter {
         std::stringstream *s;

         Deleter(std::stringstream &s) : s(&s) {}

         void operator()(unsigned int i) const {
             (*this->s) << "delete" << i;
         }
     };

    Deleter d(s);

    {
        auto r = makeScopedResource(12, d);
        auto r2 = std::move(r);
        r = std::move(r2);
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12", s.str()));
}

TEST(resource, moveAssign2) {
    std::stringstream s;
    struct Deleter {
        std::stringstream *s;

        Deleter(std::stringstream &s) : s(&s) {}

        void operator()(unsigned int i) const {
            (*this->s) << "delete" << i;
        }
    };

    Deleter d(s);

    {
        auto r = makeScopedResource(12, d);
        auto r2 = std::move(r);
        r = std::move(r2);
        r = std::move(r2);
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12", s.str()));
}

TEST(resource, rest) {
    std::stringstream s;

    {
        auto r = makeScopedResource(
                12, [&s](unsigned int i) {
                    s << "delete" << i;
                });
        r.reset(100);
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("delete12delete100", s.str()));
}

TEST(resource, release) {
    std::stringstream s;

    {
        auto r = makeScopedResource(
                12, [&s](unsigned int i) {
                    s << "delete" << i;
                });
        r.release();
    }

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("", s.str()));
}

struct AAAOp;

class AAAImpl {
private:
    long count{0};

    friend AAAOp;
public:
    virtual ~AAAImpl() = default;

    virtual const char *name() const {
        return "AAA";
    }
};

struct BBBImpl : public AAAImpl {
    ~BBBImpl() override = default;

    const char *name() const override {
        return "BBB";
    }
};

struct AAAOp {
    static long useCount(const AAAImpl *ptr) noexcept {
        return ptr->count;
    }

    static void increase(AAAImpl *ptr) noexcept {
        if(ptr != nullptr) {
            ptr->count++;
        }
    }

    static void decrease(AAAImpl *ptr) noexcept {
        if(ptr != nullptr && --ptr->count == 0) {
            delete ptr;
        }
    }
};


using AAA = IntrusivePtr<AAAImpl, AAAOp>;
using BBB = IntrusivePtr<BBBImpl, AAAOp>;

TEST(intrusive, base) {
    static_assert(sizeof(AAA) == sizeof(long), "");
    static_assert(sizeof(BBB) == sizeof(long), "");

    auto a = AAA::create();
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("AAA", a->name()));
    auto b = BBB::create();
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("BBB", b->name()));

    a = b;
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("BBB", a->name()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, a.useCount()));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

