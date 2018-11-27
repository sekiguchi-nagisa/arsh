#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include <dirent.h>
#include <sys/types.h>
#include <pwd.h>

#include <config.h>
#include "../test_common.h"
#include "../../src/constant.h"
#include "../../src/misc/fatal.h"


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

    auto e = format("%s/include5.ds:2: [semantic error] not found module: `mod4extra5.ds'\n"
                    "source mod4extra5.ds as mod\n"
                    "       ^~~~~~~~~~~~~\n"
                    "(string):2: [note] at module import\n"
                    "        source include5.ds\n"
                    "               ^~~~~~~~~~~\n", SYSTEM_MOD_DIR);
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

    ~FileFactory() {
        remove(this->name.c_str());
    }

    const std::string &getFileName() const {
        return this->name;
    }
};

struct RCTest : public InteractiveBase {
    RCTest() : InteractiveBase(BIN_PATH, ".") {}
};

static std::string getHOME() {
    std::string str;
    struct passwd *pw = getpwuid(getuid());
    if(pw == nullptr) {
        fatal_perror("getpwuid failed");
    }
    str = pw->pw_dir;
    return str;
}

#define XSTR(v) #v
#define STR(v) XSTR(v)

#define PROMPT "ydsh-" STR(X_INFO_MAJOR_VERSION) "." STR(X_INFO_MINOR_VERSION) "$ "

TEST_F(RCTest, rcfile1) {
    std::string rcpath = getHOME();
    rcpath += "/.ydshrc";
    FileFactory fileFactory(rcpath.c_str(), "var RC_VAR = 'rcfile: ~/.ydshrc'");

    this->invoke("--quiet");
    ASSERT_NO_FATAL_FAILURE(this->expect(PROMPT));
    ASSERT_NO_FATAL_FAILURE(this->sendAndExpect("assert $RC_VAR == 'rcfile: ~/.ydshrc'; exit 23"));
    ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(23, WaitStatus::EXITED));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}