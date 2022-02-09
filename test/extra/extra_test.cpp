#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <dirent.h>
#include <pwd.h>
#include <sys/types.h>

#include "../../src/constant.h"
#include "../../src/misc/fatal.h"
#include "../test_common.h"
#include <config.h>
#include <ydsh/ydsh.h>

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

using namespace ydsh;

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

  auto e = format("%s/include5.ds:2:8: [semantic error] module not found: `mod4extra5.ds'\n"
                  "source mod4extra5.ds as mod\n"
                  "       ^~~~~~~~~~~~~\n"
                  "(string):2:16: [note] at module import\n"
                  "        source include5.ds\n"
                  "               ^~~~~~~~~~~\n",
                  X_MODULE_DIR);
  ASSERT_NO_FATAL_FAILURE(this->expect(ds(src), 1, "", e.c_str()));
}

class FileFactory {
private:
  std::string name;

public:
  /**
   *
   * @param name
   * must be full path
   * @param content
   */
  FileFactory(const char *name, const std::string &content) : name(name) {
    FILE *fp = fopen(this->name.c_str(), "w");
    fwrite(content.c_str(), sizeof(char), content.size(), fp);
    fflush(fp);
    fclose(fp);
  }

  ~FileFactory() { remove(this->name.c_str()); }

  const std::string &getFileName() const { return this->name; }
};

#define XSTR(v) #v
#define STR(v) XSTR(v)

struct RCTest : public InteractiveShellBase {
  RCTest() : InteractiveShellBase(BIN_PATH, ".") {
    std::string v = "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION);
    v += (getuid() == 0 ? "# " : "$ ");
    this->setPrompt(v);
  }
};

static std::string getHOME() {
  std::string str;
  struct passwd *pw = getpwuid(getuid());
  if (pw == nullptr) {
    fatal_perror("getpwuid failed");
  }
  str = pw->pw_dir;
  return str;
}

TEST_F(RCTest, rcfile1) {
  std::string rcpath = getHOME();
  rcpath += "/.ydshrc";
  FileFactory fileFactory(rcpath.c_str(), "var RC_VAR = 'rcfile: ~/.ydshrc'");

  this->invoke("--quiet");
  ASSERT_NO_FATAL_FAILURE(this->expect(this->prompt));
  ASSERT_NO_FATAL_FAILURE(this->sendLineAndWait("assert $RC_VAR == 'rcfile: ~/.ydshrc'; exit 23",
                                                23, WaitStatus::EXITED));
}

struct APITest : public ExpectOutput {
  DSState *state{nullptr};

  APITest() { this->state = DSState_create(); }

  ~APITest() override { DSState_delete(&this->state); }
};

TEST_F(APITest, modFullpath) {
  DSError e;
  int r = DSState_loadModule(this->state, "edit", DS_MOD_FULLPATH, &e); // not load 'edit'
  ASSERT_EQ(1, r);
  ASSERT_EQ(DS_ERROR_KIND_FILE_ERROR, e.kind);
  ASSERT_STREQ(strerror(ENOENT), e.name);
  ASSERT_EQ(0, e.lineNum);
  DSError_release(&e);
}

TEST_F(APITest, mod) {
  DSError e;
  int r = DSState_loadModule(this->state, "edit", 0, &e);
  ASSERT_EQ(0, r);
  ASSERT_EQ(DS_ERROR_KIND_SUCCESS, e.kind);
  ASSERT_EQ(0, e.lineNum);
  DSError_release(&e);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}