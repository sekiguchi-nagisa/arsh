#include <gtest/gtest.h>

#include <misc/argv.hpp>

using namespace ydsh::argv;

enum class Kind : unsigned int {
    A, B, C, D, E,
};

typedef Option<Kind> Opt;
typedef CmdLines<Kind> CL;

typedef std::vector<const char *> RestArgs;

void addArg(std::vector<std::string> &) {
}

template <typename... T>
void addArg(std::vector<std::string> &args, const char *first, T ...rest) {
    args.push_back(first);
    addArg(args, rest...);
}

template <typename... T>
std::vector<std::string> make_args(const char *first, T ...rest) {
    std::vector<std::string> args;
    args.push_back("<dummy>");
    addArg(args, first, rest...);
    return args;
}


class ArgTest : public ::testing::Test {
public:
    RestArgs rest;

    ArgTest() = default;
    virtual ~ArgTest() = default;

    virtual void expectError(std::vector<std::string> args, const Opt (&options)[5], const char *expect) {
        int argc = args.size();
        char *argv[argc];
        for(int i = 0; i < argc; i++) {
            argv[i] = const_cast<char *>(args[i].c_str());
        }

        SCOPED_TRACE("");

        ASSERT_TRUE(expect != nullptr);
        try {
            CL cl;
            parseArgv(argc, argv, options, cl);
            ASSERT_TRUE(false);
        } catch(const ParseError &e) {
            ASSERT_STREQ(expect, e.getMessage().c_str());
        }
    }

    virtual void parse(std::vector<std::string> args, const Opt (&options)[5], CL &cl) {
        int argc = args.size();
        char *argv[argc];
        for(int i = 0; i < argc; i++) {
            argv[i] = const_cast<char *>(args[i].c_str());
        }

        SCOPED_TRACE("");

        try {
            int index = parseArgv(argc, argv, options, cl);
            ASSERT_TRUE(index <= argc && index > 0);
            for(; index < argc; index++) {
                this->rest.push_back(argv[index]);
            }
        } catch(const ParseError &e) {
            ASSERT_TRUE(false);
        }
    }
};

TEST_F(ArgTest, fail1) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", 0, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-a", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->expectError(make_args("-a", "-c"), options, "duplicated option: -a");
    });
}

TEST_F(ArgTest, fail2) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", 0, "hogehjoge"},
            {Kind::C, "c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-a", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->expectError(make_args("-a", "fre"), options, "illegal option name: c");
    });
}

TEST_F(ArgTest, fail3) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", 0, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->expectError(make_args("-f", "fre"), options, "illegal option: -f");
    });
}

TEST_F(ArgTest, fail4) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->expectError(make_args("-a", "-b"), options, "need argument: -b");
    });
}

TEST_F(ArgTest, fail5) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->expectError(make_args("-a", "-b", "-ae"), options, "need argument: -b");
    });
}

TEST_F(ArgTest, fail6) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", REQUIRE | HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", REQUIRE | HAS_ARG, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        this->expectError(make_args("-a", "-b", "hoge", "-e", "huga"), options, "require option: -c");
    });
}

TEST_F(ArgTest, success1) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        CL cl;
        this->parse(make_args("-a", "-b", "ae"), options, cl);
        ASSERT_EQ(2u, cl.size());
        ASSERT_EQ(Kind::A, cl[0].first);
        ASSERT_EQ(Kind::B, cl[1].first);
        ASSERT_STREQ("ae", cl[1].second);

        ASSERT_EQ(0u, this->rest.size());
    });
}

TEST_F(ArgTest, success2) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        CL cl;
        this->parse(make_args("-a"), options, cl);
        ASSERT_EQ(1u, cl.size());
        ASSERT_EQ(Kind::A, cl[0].first);

        ASSERT_EQ(0u, this->rest.size());
    });
}

TEST_F(ArgTest, succes3) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        CL cl;
        std::vector<std::string> args;
        args.push_back("<dummy>");
        this->parse(args, options, cl);
        ASSERT_EQ(0u, cl.size());

        ASSERT_EQ(0u, this->rest.size());
    });
}

TEST_F(ArgTest, success4) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        CL cl;
        this->parse(make_args("ae"), options, cl);
        ASSERT_EQ(0u, cl.size());

        ASSERT_EQ(1u, this->rest.size());
        ASSERT_STREQ("ae", this->rest[0]);
    });
}

TEST_F(ArgTest, success5) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", 0, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        CL cl;
        this->parse(make_args("ae", "he", "-a"), options, cl);
        ASSERT_EQ(0u, cl.size());

        ASSERT_EQ(3u, this->rest.size());
        ASSERT_STREQ("ae", this->rest[0]);
        ASSERT_STREQ("he", this->rest[1]);
        ASSERT_STREQ("-a", this->rest[2]);
    });
}

TEST_F(ArgTest, success6) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", IGNORE_REST, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        CL cl;
        this->parse(make_args("-b", "he", "-a", "-d", "-e"), options, cl);
        ASSERT_EQ(3u, cl.size());
        ASSERT_EQ(Kind::B, cl[0].first);
        ASSERT_STREQ("he", cl[0].second);
        ASSERT_EQ(Kind::A, cl[1].first);
        ASSERT_STREQ("", cl[1].second);
        ASSERT_EQ(Kind::D, cl[2].first);

        ASSERT_EQ(1u, this->rest.size());
        ASSERT_STREQ("-e", this->rest[0]);
    });
}

TEST_F(ArgTest, success7) {
    static const Opt options[] = {
            {Kind::A, "-a", 0, "hogehjoge"},
            {Kind::B, "-b", HAS_ARG, "hogehjoge"},
            {Kind::C, "-c", 0, "hogehjogef"},
            {Kind::D, "-d", IGNORE_REST | HAS_ARG, "hogehjoges"},
            {Kind::E, "-e", 0, "hogehjogee"},
    };

    ASSERT_NO_FATAL_FAILURE({
        SCOPED_TRACE("");

        CL cl;
        this->parse(make_args("-b", "he", "-a", "-d", "e", "-f", "hu"), options, cl);
        ASSERT_EQ(3u, cl.size());
        ASSERT_EQ(Kind::B, cl[0].first);
        ASSERT_STREQ("he", cl[0].second);
        ASSERT_EQ(Kind::A, cl[1].first);
        ASSERT_STREQ("", cl[1].second);
        ASSERT_EQ(Kind::D, cl[2].first);
        ASSERT_STREQ("e", cl[2].second);

        ASSERT_EQ(2u, this->rest.size());
        ASSERT_STREQ("-f", this->rest[0]);
        ASSERT_STREQ("hu", this->rest[1]);
     });
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
