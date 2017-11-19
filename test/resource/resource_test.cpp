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


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

