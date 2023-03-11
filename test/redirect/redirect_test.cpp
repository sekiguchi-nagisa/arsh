#include "gtest/gtest.h"

#include "../test_common.h"

#ifndef BIN_PATH
#define BIN_PATH "./ydsh"
#endif

using namespace ydsh;

class RedirectTest : public ExpectOutput, public TempFileFactory {
private:
  std::string targetName;

public:
  RedirectTest() : INIT_TEMP_FILE_FACTORY(redirect_test) {
    this->targetName += this->getTempDirName();
    this->targetName += "/target";
  }

  ~RedirectTest() override = default;

  const char *getTargetName() const { return this->targetName.c_str(); }

  using ExpectOutput::expect;

  void contentEq(const char *str) const {
    // read file contents
    ByteBuffer buffer;
    char data[256];
    FILE *fp = fopen(this->getTargetName(), "r");
    ASSERT_TRUE(fp != nullptr);
    int fd = fileno(fp);
    while (true) {
      ssize_t readSize = read(fd, data, std::size(data));
      if (readSize > 0) {
        buffer.append(data, readSize);
      }
      if (readSize == -1 && (errno == EAGAIN || errno == EINTR)) {
        continue;
      }
      if (readSize <= 0) {
        break;
      }
    }
    fclose(fp);

    // compare
    std::string content(buffer.data(), buffer.size());
    ASSERT_STREQ(str, content.c_str());
  }
};

#define CL(...)                                                                                    \
  ProcBuilder { BIN_PATH, "-c", format(__VA_ARGS__).c_str() }

TEST_F(RedirectTest, STDIN) {
  // create target file
  ProcBuilder builder = {
      "sh",
      "-c",
      format("echo hello world > %s", this->getTargetName()).c_str(),
  };
  auto pair = builder.exec();
  ASSERT_EQ(WaitStatus::EXITED, pair.kind);
  ASSERT_EQ(0, pair.value);

  // builtin
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("__gets < %s", this->getTargetName()), 0, "hello world\n"));

  // external
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("grep < %s 'hello world'", this->getTargetName()), 0, "hello world\n"));

  // user-defined
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("cat2() { cat; }; cat2 < %s", this->getTargetName()), 0, "hello world\n"));

  // call
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call __gets < %s", this->getTargetName()), 0, "hello world\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call grep < %s 'hello world'", this->getTargetName()), 0, "hello world\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("cat2() { cat; }; call cat2 < %s", this->getTargetName()),
                                       0, "hello world\n"));

  // with
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("{ grep 'hello world'; } with < %s", this->getTargetName()), 0, "hello world\n"));

  // command
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __gets < %s", this->getTargetName()), 0, "hello world\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("command grep < %s 'hello world'", this->getTargetName()),
                                       0, "hello world\n"));
}

TEST_F(RedirectTest, STDOUT) {
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA"), 0, "AAA\n"));

  // builtin command
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 ABC > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("__puts -1 123 1> %s; echo world", this->getTargetName()), 0, "world\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 DEF >> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 GHI 1>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // external command
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("sh -c 'echo ABC' > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("sh -c 'echo 123' 1> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c 'echo DEF' >> %s; echo hello", this->getTargetName()), 0, "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("sh -c 'echo GHI' 1>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // user-defined
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { echo ABC; }; echo2 > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { echo 123; }; echo2 1> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("echo2() { echo DEF; }; echo2 >> %s; echo hello", this->getTargetName()), 0, "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { echo GHI; }; echo2 1>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // call builtin
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("call __puts -1 ABC > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("call __puts -1 123 1> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call __puts -1 DEF >> %s; echo 123", this->getTargetName()), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("call __puts -1 GHI 1>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // call external
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("call sh -c 'echo ABC' > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c 'echo 123' 1> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c 'echo DEF' >> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("call sh -c 'echo GHI' 1>> %s; echo test", this->getTargetName()), 0, "test\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // call user-defined
  // user-defined
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { echo ABC; }; call echo2 > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { echo 123; }; call echo2 1> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { echo DEF; }; call echo2 >> %s; echo hello", this->getTargetName()),
                   0, "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { echo GHI; }; call echo2 1>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // with
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("{ echo ABC; } with > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("{ 34; } with > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq(""));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -2 hey; } with > %s", this->getTargetName()), 0, "", "hey\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq(""));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("{ echo 123; } with 1> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ echo DEF; } with >> %s; echo hoge", this->getTargetName()), 0, "hoge\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("{ echo GHI; } with 1>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // command builtin
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command __puts -1 ABC > %s; echo hello", this->getTargetName()), 0, "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command __puts -1 123 1> %s; echo hello", this->getTargetName()), 0, "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command __puts -1 DEF >> %s; echo hello", this->getTargetName()), 0, "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command __puts -1 GHI 1>> %s; echo hello", this->getTargetName()), 0, "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));

  // command external
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c 'echo ABC' > %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("ABC\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command sh -c 'echo 123' 1> %s; echo world", this->getTargetName()), 0, "world\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c 'echo DEF' >> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c 'echo GHI' 1>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nDEF\nGHI\n"));
}

TEST_F(RedirectTest, STDERR) {
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -2 AAA"), 0, "", "AAA\n"));

  // builtin
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("__puts -2 123 2> %s; __puts -2 hello", this->getTargetName()), 0, "", "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("__puts -2 ABC 002>> %s; __puts -2 hello", this->getTargetName()), 0, "", "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // external
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c 'echo 123 1>&2' 2> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("sh -c 'echo ABC 1>&2' 2>> %s; sh -c 'echo ABCD 1>&2'", this->getTargetName()), 0, "",
      "ABCD\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // user-defined
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("echo2() { sh -c 'echo 123 1>&2'; }; echo2 2> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("echo2() { sh -c 'echo ABC 1>&2'; }; echo2 2>> %s; sh -c 'echo ABCD 1>&2'",
                      this->getTargetName()),
                   0, "", "ABCD\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // call builtin
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("call __puts -2 123 2> %s; __puts -2 hello", this->getTargetName()), 0, "", "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("call __puts -2 ABC 2>> %s; __puts -2 hello", this->getTargetName()), 0, "", "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // call external
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c 'echo 123 1>&2' 2> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c 'echo ABC 1>&2' 2>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // call user-defined
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("echo2() { sh -c 'echo 123 1>&2'; }; call echo2 2> %s; echo hey", this->getTargetName()),
      0, "hey\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("echo2() { sh -c 'echo ABC 1>&2'; }; call echo2 2>> %s; sh -c 'echo ABCD 1>&2'",
         this->getTargetName()),
      0, "", "ABCD\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // with
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ sh -c 'echo 123 1>&2'; } with 2> %s; __puts -1 AAA; __puts -2 BBB",
                      this->getTargetName()),
                   0, "AAA\n", "BBB\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("{ __puts -2 ABC; } with 2>> %s; __puts -2 AAA", this->getTargetName()), 0, "", "AAA\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // command builtin
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command __puts -2 123 2> %s; __puts -2 hello", this->getTargetName()), 0, "", "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -2 ABC 2>> %s; __puts -2 hello", this->getTargetName()), 0,
                   "", "hello\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));

  // command external
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c 'echo 123 1>&2' 2> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c 'echo ABC 1>&2' 2>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("123\nABC\n"));
}

TEST_F(RedirectTest, merge) {
  // builtin command
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 2>&01"), 0, "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 2>&1 > /dev/null"), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 2>&1 2> /dev/null"), 0, "AAA\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 1>& 0002"), 0, "", "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("__puts -1 AAA -2 123 1>&2 > /dev/null"), 0, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("__puts -1 AAA -2 123 1>&2 2> /dev/null"), 0, "", "AAA\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 &> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("__puts -1 AAA -2 123 &>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\nAAA\n123\n"));

  // external command
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c \"echo AAA && echo 123 1>&2\" 2>&1"), 0, "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c \"echo AAA && echo 123 1>&2\" 2>&1 > /dev/null"), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c \"echo AAA && echo 123 1>&2\" 2>&1 2> /dev/null"), 0, "AAA\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c \"echo AAA && echo 123 1>&2\" 1>&2"), 0, "", "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c \"echo AAA && echo 123 1>&2\" 1>&2 > /dev/null"), 0, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c \"echo AAA && echo 123 1>&2\" 1>&2 2> /dev/null"), 0, "", "AAA\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c 'echo AAA && echo 123 1>&2' &> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("sh -c 'echo AAA && echo 123 1>&2' &>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\nAAA\n123\n"));

  // call
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c \"echo AAA && echo 123 1>&2\" 2>&1"), 0, "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c \"echo AAA && echo 123 1>&2\" 2>&1 > /dev/null"), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c \"echo AAA && echo 123 1>&2\" 2>&1 2> /dev/null"), 0, "AAA\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c \"echo AAA && echo 123 1>&2\" 1>&2"), 0, "", "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("call sh -c \"echo AAA && echo 123 1>&2\" 1>&2 > /dev/null"), 0, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("call sh -c \"echo AAA && echo 123 1>&2\" 1>&2 2> /dev/null"), 0, "", "AAA\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c 'echo AAA && echo 123 1>&2' &> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("call sh -c 'echo AAA && echo 123 1>&2' &>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\nAAA\n123\n"));

  // with
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("{ __puts -1 AAA -2 123; } with 2>&1"), 0, "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -1 AAA -2 123; } with 2>&1 > /dev/null"), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -1 AAA -2 123; } with 2>&1 2> /dev/null"), 0, "AAA\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -1 AAA -2 123; } with 001>& 2"), 0, "", "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -1 AAA -2 123; } with 1<&2 > /dev/null"), 0, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -1 AAA -2 123; } with 1>&2 2> /dev/null"), 0, "", "AAA\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -1 AAA -2 123; } with &> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("{ __puts -1 AAA -2 123; } with &>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\nAAA\n123\n"));

  // command command
  // builtin command
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("command __puts -1 AAA -2 123 2>&1"), 0, "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -1 AAA -2 123 02>&1 > /dev/null"), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -1 AAA -2 123 2<& 1 2> /dev/null"), 0, "AAA\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -1 AAA -2 123 >&2"), 0, "", "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -1 AAA -2 123 001>& 2 > /dev/null"), 0, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -1 AAA -2 123 1>&2 2> /dev/null"), 0, "", "AAA\n"));

  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -1 AAA -2 123 &> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command __puts -1 AAA -2 123 &>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\nAAA\n123\n"));

  // external command
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c \"echo AAA && echo 123 1>& 2\" 2>&1"), 0, "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c \"echo AAA && echo 123 1>&2\" 2>&1 > /dev/null"), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command sh -c \"echo AAA && echo 123 1>&2\" 2>&1 2> /dev/null"), 0, "AAA\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("command sh -c \"echo AAA && echo 123 1>&2\" 1>&2"), 0, "", "AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command sh -c \"echo AAA && echo 123 1>&2\" 0001>&2 > /dev/null"), 0, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command sh -c \"echo AAA && echo 123 1>&2\" 1>& 2 2> /dev/null"), 0, "", "AAA\n"));

  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command sh -c 'echo AAA && echo 123 1>&2' &> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("command sh -c 'echo AAA && echo 123 1>&2' &>> %s", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n123\nAAA\n123\n"));

  // pipeline
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 | grep AAA"), 0, "AAA\n", "123\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("__puts -1 AAA -2 123 1> /dev/null | grep AAA"), 1, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(
      CL("__puts -1 AAA -2 123 1> /dev/null | grep AAA 2> /dev/null"), 1, "", "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 1> /dev/null 2>&1 | grep AAA"), 1));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 2>& 1 | grep 123"), 0, "123\n"));
  ASSERT_NO_FATAL_FAILURE(this->expect(CL("__puts -1 AAA -2 123 2>&1 1> /dev/null | grep AAA"), 1));
}

TEST_F(RedirectTest, clobber) {
  auto v = CL(R"(
    echo hello > %s

    shctl unset clobber
    echo world1 >| %s
    echo world2 > %s
)",
              this->getTargetName(), this->getTargetName(), this->getTargetName());
  auto err = format(R"([runtime error]
SystemError: io redirection failed: %s, caused by `%s'
    from (string):6 '<toplevel>()'
)",
                    this->getTargetName(), strerror(EEXIST));
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(v), 1, "", err));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("world1\n"));

  v = CL(R"(
    echo hello > %s

    shctl unset clobber
    __puts -1 world1 -2 !! &>| %s
    __puts -1 world2 -2 !! &> %s
)",
         this->getTargetName(), this->getTargetName(), this->getTargetName());
  err = format(R"([runtime error]
SystemError: io redirection failed: %s, caused by `%s'
    from (string):6 '<toplevel>()'
)",
               this->getTargetName(), strerror(EEXIST));
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(v), 1, "", err));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("world1\n!!\n"));

  v = CL(R"(
    echo hello > %s

    shctl unset clobber
    __puts -1 world1 -2 !! >> %s   # append existing file even if clobber is unset
    __puts -1 world2 -2 !! &>> %s
)",
         this->getTargetName(), this->getTargetName(), this->getTargetName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(v), 0, "", "!!\n"));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("hello\nworld1\nworld2\n!!\n"));
}

TEST_F(RedirectTest, fd) {
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("var a = new UnixFD('%s'); echo -n 'hello ' >& $a;\n echo world 1>& $a",
                      this->getTargetName()),
                   0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("hello world\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("var a = new UnixFD('%s'); echo 12345 1>& $a", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("12345\nworld\n"));
  ASSERT_NO_FATAL_FAILURE(
      this->expect(CL("var a = new UnixFD('%s'); __puts -2 AAA 2>& $a", this->getTargetName()), 0));
  ASSERT_NO_FATAL_FAILURE(this->contentEq("AAA\n5\nworld\n"));

  auto v = CL(R"(
    var a = new UnixFD('%s')
    var r = $(cat <& $a)
    assert $r.size() == 3
    assert $r[0] == "AAA"
    assert $r[1] == "5"
    assert $r[2] == "world"
)",
              this->getTargetName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(v), 0));

  v = CL(R"(
    var a = new UnixFD('%s')
    var r = $(cat 0000<&$a)
    assert $r.size() == 3
    assert $r[0] == "AAA"
    assert $r[1] == "5"
    assert $r[2] == "world"
)",
         this->getTargetName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(v), 0));

  v = CL("var a = new UnixFD('%s')\n"
         "var r = new [String]()\n"
         "while(read -u $a) { $r.add($REPLY); }\n"
         "true\n"
         "assert $r.size() == 3\n"
         "assert $r[0] == 'AAA'\n"
         "assert $r[1] == '5'\n"
         "assert $r[2] == 'world'",
         this->getTargetName());
  ASSERT_NO_FATAL_FAILURE(this->expect(std::move(v), 0));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
