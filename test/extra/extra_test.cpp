#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>

#include "../../src/constant.h"
#include "../../src/misc/fatal.h"
#include "../test_common.h"
#include <arsh/arsh.h>
#include <config.h>

#ifndef BIN_PATH
#error require BIN_PATH
#endif

#ifndef EXTRA_TEST_DIR
#error require EXTRA_TEST_DIR
#endif

/**
 * extra test cases dependent on system directory structure
 * and have side effect on directory structures
 */

using namespace arsh;

struct ModLoadTest : public ExpectOutput, public TempFileFactory {
  ModLoadTest() : INIT_TEMP_FILE_FACTORY(extra_test) {}
};

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
                    "assert test -f $MODULE_HOME/mod4extra1.ds\n"
                    "assert test -f $MODULE_HOME/mod4extra2.ds\n"
                    "assert !test -f $MODULE_HOME/mod4extra3.ds\n"
                    "assert test -f %s/mod4extra1.ds\n"
                    "assert test -f %s/mod4extra2.ds\n"
                    "assert test -f %s/mod4extra3.ds\n"
                    "true",
                    X_MODULE_DIR, X_MODULE_DIR, X_MODULE_DIR);

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

  auto e = format("[semantic error] module not found: `mod4extra5.ds'\n"
                  " --> %s/include5.ds:2:8\n"
                  "source mod4extra5.ds as mod\n"
                  "       ^~~~~~~~~~~~~\n"
                  "[note] at module import\n"
                  " --> (string):2:16\n"
                  "        source include5.ds\n"
                  "               ^~~~~~~~~~~\n",
                  X_MODULE_DIR);
  ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 1, "", e.c_str()));
}

struct APITest : public ExpectOutput {
  ARState *state{nullptr};

  APITest() { this->state = ARState_create(); }

  ~APITest() override { ARState_delete(&this->state); }
};

TEST_F(APITest, modFullpath) {
  ARError e;
  int r = ARState_loadModule(this->state, "repl", AR_MOD_FULLPATH, &e); // not load 'repl'
  ASSERT_EQ(1, r);
  ASSERT_EQ(AR_ERROR_KIND_FILE_ERROR, e.kind);
  ASSERT_STREQ(strerror(ENOENT), e.name);
  ASSERT_EQ(0, e.lineNum);
  ARError_release(&e);
}

TEST_F(APITest, mod) {
  ARError e;
  int r = ARState_loadModule(this->state, "repl", 0, &e);
  ASSERT_EQ(0, r);
  ASSERT_EQ(AR_ERROR_KIND_SUCCESS, e.kind);
  ASSERT_EQ(0, e.lineNum);
  ARError_release(&e);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}