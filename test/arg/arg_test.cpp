#include "gtest/gtest.h"

#include <misc/argv.hpp>

using namespace ydsh::argv;

enum class Kind : unsigned int {
    A, B, C, D, E,
};

using Opt = Option<Kind>;
using CL = CmdLines<Kind>;

using RestArgs = std::vector<const char *>;

template <typename ...T>
std::vector<const char *> make_args(T && ...rest) {
    return {"<dummy>", std::forward<T>(rest)...};
}


class ArgTest : public ::testing::Test {
public:
    RestArgs rest;

    ArgTest() = default;
    virtual ~ArgTest() = default;

    virtual void expectError(std::vector<const char *> &&args, ArgvParser<Kind> &parser, const char *expect) {
        if(!parser.hasError()) {
            int argc = args.size();
            char *argv[argc];
            for(int i = 0; i < argc; i++) {
                argv[i] = const_cast<char *>(args[i]);
            }

            SCOPED_TRACE("");

            ASSERT_TRUE(expect != nullptr);

            CL cl;
            int index = parser(argc, argv, cl);
            ASSERT_EQ(-1, index);
        }

        ASSERT_TRUE(parser.hasError());
        ASSERT_STREQ(expect, parser.getErrorMessage());
    }

    virtual void parse(std::vector<const char *> &&args, ArgvParser<Kind> &parser, CL &cl) {
        int argc = args.size();
        char *argv[argc];
        for(int i = 0; i < argc; i++) {
            argv[i] = const_cast<char *>(args[i]);
        }

        SCOPED_TRACE("");

        int index = parser(argc, argv, cl);
        ASSERT_TRUE(index <= argc && index > 0);
        ASSERT_FALSE(parser.hasError());
        for(; index < argc; index++) {
            this->rest.push_back(argv[index]);
        }
    }
};

TEST_F(ArgTest, fail1) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", 0, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-a", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE(this->expectError(make_args("-a", "-c"), parser, "duplicated option: -a"));
}

TEST_F(ArgTest, fail2) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", 0, "hogehjoge"},
            {Kind::C, "c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
//            {Kind::E, "-a", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE(this->expectError(make_args("-a", "fre"), parser, "illegal option name: c"));
}

TEST_F(ArgTest, fail3) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", 0, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE(this->expectError(make_args("-f", "fre"), parser, "illegal option: -f"));
}

TEST_F(ArgTest, fail4) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE(this->expectError(make_args("-a", "-b"), parser, "need argument: -b"));
}

TEST_F(ArgTest, fail5) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE(this->expectError(make_args("-a", "-b", "-ae"), parser, "need argument: -b"));
}

TEST_F(ArgTest, fail6) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", REQUIRE | HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", REQUIRE | HAS_ARG, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE(this->expectError(make_args("-a", "-b", "hoge", "-e", "huga"), parser, "require option: -c"));
}

TEST_F(ArgTest, success1) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    this->parse(make_args("-a", "-b", "ae"), parser, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, cl.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::A, cl[0].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::B, cl[1].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ae", cl[1].second));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, this->rest.size()));
}

TEST_F(ArgTest, success2) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    this->parse(make_args("-a"), parser, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, cl.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::A, cl[0].first));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, this->rest.size()));
}

TEST_F(ArgTest, succes3) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    this->parse(make_args(), parser, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, cl.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, this->rest.size()));
}

TEST_F(ArgTest, success4) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    this->parse(make_args("ae"), parser, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, cl.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, this->rest.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ae", this->rest[0]));
}

TEST_F(ArgTest, success5) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    this->parse(make_args("ae", "he", "-a"), parser, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0u, cl.size()));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, this->rest.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("ae", this->rest[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("he", this->rest[1]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("-a", this->rest[2]));
}

TEST_F(ArgTest, success6) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", IGNORE_REST, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    this->parse(make_args("-b", "he", "-a", "-d", "-e"), parser, cl);
    ASSERT_EQ(3u, cl.size());
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::B, cl[0].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("he", cl[0].second));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::A, cl[1].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("", cl[1].second));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::D, cl[2].first));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(1u, this->rest.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("-e", this->rest[0]));
}

TEST_F(ArgTest, success7) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", IGNORE_REST | HAS_ARG, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    this->parse(make_args("-b", "he", "-a", "-d", "e", "-f", "hu"), parser, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3u, cl.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::B, cl[0].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("he", cl[0].second));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::A, cl[1].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("", cl[1].second));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(Kind::D, cl[2].first));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("e", cl[2].second));

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2u, this->rest.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("-f", this->rest[0]));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("hu", this->rest[1]));
}

template <typename T, std::size_t N>
static size_t arraySize(const T (&)[N]) {
    return N;
}

TEST_F(ArgTest, success8) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    const char *argv[] = {
            "<dummy>", "-a", "-", "hoge"
    };

    int index = parser(arraySize(argv), (char **)argv, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, index));
}

TEST_F(ArgTest, success9) {
    ArgvParser<Kind> parser = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    CL cl;
    const char *argv[] = {
            "<dummy>", "--", "-a", "hoge"
    };

    int index = parser(arraySize(argv), (char **)argv, cl);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, index));
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
