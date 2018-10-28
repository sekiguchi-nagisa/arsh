#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "../test_common.h"
#include "../../src/constant.h"


#ifndef BIN_PATH
#error require BIN_PATH
#endif

#ifndef EXTRA_TEST_DIR
#error require EXTRA_TEST_DIR
#endif

using namespace ydsh;

class ModLoadTest : public ExpectOutput {};

static ProcBuilder ds(const char *src) {
    return ProcBuilder{BIN_PATH, "-c", src}
        .setOut(IOConfig::PIPE)
        .setErr(IOConfig::PIPE)
        .setWorkingDir(EXTRA_TEST_DIR);
}

TEST_F(ModLoadTest, prepare) {
    auto src = format("assert test -f $SCRIPT_DIR/mod4extra1.ds\n"
                      "assert !test -f $SCRIPT_DIR/mod4extra2.ds\n"
                      "assert !test -f $SCRIPT_DIR/mod4extra3.ds\n"
                      "assert test -f ~/.ydsh/module/mod4extra1.ds\n"
                      "assert test -f ~/.ydsh/module/mod4extra2.ds\n"
                      "assert !test -f ~/.ydsh/module/mod4extra3.ds\n"
                      "assert test -f %s/mod4extra1.ds\n"
                      "assert test -f %s/mod4extra2.ds\n"
                      "assert test -f %s/mod4extra3.ds\n"
                      "true", SYSTEM_MOD_DIR, SYSTEM_MOD_DIR, SYSTEM_MOD_DIR);

    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src.c_str()), 0));
}

TEST_F(ModLoadTest, scriptdir) {
    const char *src = R"(
        source mod4extra1.ds
        assert $OK_LOADING == "script_dir: mod4extra1.ds"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 0));

    src = R"(
        source include1.ds
        assert $mod1.OK_LOADING == "script_dir: mod4extra1.ds"
        assert $mod2.OK_LOADING == "local: mod4extra2.ds"
        assert $mod3.OK_LOADING == "system: mod4extra3.ds"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 0, "include from script_dir!!\n"));
}

TEST_F(ModLoadTest, local) {
    const char *src = R"(
        source mod4extra2.ds
        assert $OK_LOADING == "local: mod4extra2.ds"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 0));

    src = R"(
        source include2.ds
        assert $mod1.OK_LOADING == "local: mod4extra1.ds"
        assert $mod2.OK_LOADING == "local: mod4extra2.ds"
        assert $mod3.OK_LOADING == "system: mod4extra3.ds"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 0));

    src = R"(
        source include4.ds
        assert $mod.OK_LOADING == "system: mod4extra4.ds"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 0));
}

TEST_F(ModLoadTest, system) {
    const char *src = R"(
        source mod4extra3.ds
        assert $OK_LOADING == "system: mod4extra3.ds"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 0));

    src = R"(
        source include3.ds
        assert $mod1.OK_LOADING == "system: mod4extra1.ds"
        assert $mod2.OK_LOADING == "system: mod4extra2.ds"
        assert $mod3.OK_LOADING == "system: mod4extra3.ds"
)";
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 0));

    src = R"(
        source include5.ds
        exit 100
)";

    auto e = format("%s/include5.ds:2: [semantic error] unresolved module: mod4extra5.ds, by `No such file or directory'\n"
                    "source mod4extra5.ds as mod\n"
                    "       ^~~~~~~~~~~~~\n"
                    "(string):2: [note] at module import\n"
                    "        source include5.ds\n"
                    "               ^~~~~~~~~~~\n", SYSTEM_MOD_DIR);
    ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 1, "", e.c_str()));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}