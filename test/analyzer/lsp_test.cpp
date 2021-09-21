#include "../test_common.h"

#include "client.h"
#include "lsp.h"
#include "server.h"

using namespace ydsh;
using namespace lsp;
using namespace json;
using namespace rpc;

template <typename T>
static JSON toJSON(T &v) {
  JSONSerializer serializer;
  serializer(v);
  return std::move(serializer).take();
}

template <typename T>
bool fromJSON(JSON &&json, T &v) {
  JSONDeserializer deserializer(std::move(json));
  deserializer(v);
  return !deserializer.hasError();
}

TEST(LSPTest, Position) {
  const char *text = R"(
    {
        "line" : 23, "character" : 34
    }
)";
  auto json = JSON::fromString(text);
  ASSERT_FALSE(json.isInvalid());
  auto line = json.serialize();

  Position pos;
  fromJSON(std::move(json), pos);
  ASSERT_EQ(23, pos.line);
  ASSERT_EQ(34, pos.character);
  ASSERT_EQ(line, toJSON(pos).serialize());
}

TEST(LSPTest, Range) {
  const char *text = R"(
    {
        "start" : {
            "line" : 90, "character" : 100
        },
        "end" : {
            "line" : 100, "character" : 24
        }
    }
)";
  auto json = JSON::fromString(text);
  ASSERT_FALSE(json.isInvalid());
  auto line = json.serialize();

  Range range;
  fromJSON(std::move(json), range);
  ASSERT_EQ(90, range.start.line);
  ASSERT_EQ(100, range.start.character);
  ASSERT_EQ(100, range.end.line);
  ASSERT_EQ(24, range.end.character);
  ASSERT_EQ(line, toJSON(range).serialize());
}

TEST(LSPTest, Location) {
  const char *text = R"(
    {
        "uri" : "/hoge/hoge",
        "range" : {
            "start" : {
                "line" : 90, "character" : 15
            },
            "end" : {
                "line" : 100, "character" : 24
            }
        }
    }
)";
  auto json = JSON::fromString(text);
  ASSERT_FALSE(json.isInvalid());
  auto line = json.serialize();

  Location location;
  fromJSON(std::move(json), location);
  ASSERT_EQ("/hoge/hoge", location.uri);
  ASSERT_EQ(90, location.range.start.line);
  ASSERT_EQ(15, location.range.start.character);
  ASSERT_EQ(100, location.range.end.line);
  ASSERT_EQ(24, location.range.end.character);
  ASSERT_EQ(line, toJSON(location).serialize());
}

TEST(LSPTest, LocationLink) {
  const char *text = R"(
    {
        "targetUri" : "/hoge",
        "targetRange" : {
            "start" : {
                "line" : 90, "character" : 15
            },
            "end" : {
                "line" : 100, "character" : 24
            }
        }
    }
)";
  auto json = JSON::fromString(text);
  ASSERT_FALSE(json.isInvalid());
  auto line = json.serialize();

  LocationLink link;
  fromJSON(std::move(json), link);
  ASSERT_FALSE(link.originSelectionRange.hasValue());
  ASSERT_FALSE(link.targetSelectionRange.hasValue());
  ASSERT_EQ("/hoge", link.targetUri);
  ASSERT_EQ(90, link.targetRange.start.line);
  ASSERT_EQ(15, link.targetRange.start.character);
  ASSERT_EQ(100, link.targetRange.end.line);
  ASSERT_EQ(24, link.targetRange.end.character);
  ASSERT_EQ(line, toJSON(link).serialize());
}

TEST(LSPTest, Command) {
  const char *text = R"(
    {
        "title" : "stop",
        "command" : "/stop"
    }
)";
  auto json = JSON::fromString(text);
  ASSERT_FALSE(json.isInvalid());
  auto line = json.serialize();

  Command cmd;
  fromJSON(std::move(json), cmd);
  ASSERT_EQ("stop", cmd.title);
  ASSERT_EQ("/stop", cmd.command);
  ASSERT_EQ(line, toJSON(cmd).serialize());
}

TEST(LSPTest, TextEdit) {
  const char *text = R"(
    {
        "newText" : "hello world!!",
        "range" : {
            "start" : {
                "line" : 90, "character" : 15
            },
            "end" : {
                "line" : 100, "character" : 24
            }
        }
    }
)";
  auto json = JSON::fromString(text);
  ASSERT_FALSE(json.isInvalid());
  auto line = json.serialize();

  TextEdit edit;
  fromJSON(std::move(json), edit);
  ASSERT_EQ("hello world!!", edit.newText);
  ASSERT_EQ(90, edit.range.start.line);
  ASSERT_EQ(15, edit.range.start.character);
  ASSERT_EQ(100, edit.range.end.line);
  ASSERT_EQ(24, edit.range.end.character);
  ASSERT_EQ(line, toJSON(edit).serialize());
}

static void writeAndSeekToHead(const FilePtr &file, const std::string &line) {
  writeAll(file, line);
  fflush(file.get());
  fseek(file.get(), 0L, SEEK_SET);
}

static std::string readAfterSeekHead(const FilePtr &file) {
  std::string ret;
  fseek(file.get(), 0L, SEEK_SET);
  readAll(file, ret);
  return ret;
}

static void clearFile(const FilePtr &file) {
  int fd = fileno(file.get());
  int s = ftruncate(fd, 0);
  (void)s;
  fseek(file.get(), 0L, SEEK_SET);
}

struct TransportTest : public ::testing::Test {
  LSPLogger logger;
  LSPTransport transport;

  TransportTest() : transport(this->logger, createFilePtr(tmpfile), createFilePtr(tmpfile)) {
    this->logger.setSeverity(LogLevel::INFO);
    this->logger.setAppender(createFilePtr(tmpfile));
  }

  void setInput(const std::string &str) const {
    writeAndSeekToHead(this->transport.getInput(), str);
  }

  std::string readLog() const { return readAfterSeekHead(this->logger.getAppender()); }

  std::string readOutput() const { return readAfterSeekHead(this->transport.getOutput()); }
};

TEST_F(TransportTest, case1) {
  this->setInput("hoge");
  int size = this->transport.recvSize();
  ASSERT_EQ(-1, size);
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: hoge.+"));
}

TEST_F(TransportTest, case2) {
  this->setInput("hoge\r");
  ASSERT_EQ(-1, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: hoge.+"));
}

TEST_F(TransportTest, case3) {
  this->setInput("hoge\n");
  ASSERT_EQ(-1, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: hoge.+"));
}

TEST_F(TransportTest, case4) {
  this->setInput("hoge: 34\r\n");
  ASSERT_EQ(-1, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+other header: hoge: 34.+"));
}

TEST_F(TransportTest, case5) {
  this->setInput("Content-Length: hey\r\n");
  ASSERT_EQ(-1, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+may be broken content length.+"));
}

TEST_F(TransportTest, case6) {
  this->setInput("Content-Length: 12\r\nContent-Length: 5600\r\n");
  ASSERT_EQ(-1, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+previous read message length: 12.+"));
}

TEST_F(TransportTest, case7) {
  this->setInput("Content-Length: 12\r\nContent-Length: 5\r\n\r\n12345");
  int size = this->transport.recvSize();
  ASSERT_EQ(5, size);
  auto logStr = this->readLog();
  ASSERT_THAT(logStr, ::testing::MatchesRegex(".+Content-Length: 5.+"));

  char data[5];
  ASSERT_EQ(5, this->transport.recv(std::size(data), data));
  std::string str(data, 5);
  ASSERT_EQ("12345", str);
}

TEST_F(TransportTest, case8) {
  this->setInput("Content-Length: 5\r\n\r\n12345");
  int size = this->transport.recvSize();
  ASSERT_EQ(5, size);
  auto logStr = this->readLog();
  ASSERT_THAT(logStr, ::testing::MatchesRegex(".+Content-Length: 5.+"));

  char data[3];
  int recvSize = this->transport.recv(std::size(data), data);
  ASSERT_EQ(3, recvSize);
  std::string str(data, recvSize);
  ASSERT_EQ("123", str);

  recvSize = this->transport.recv(std::size(data), data);
  ASSERT_EQ(2, recvSize);
  str = std::string(data, recvSize);
  ASSERT_EQ("45", str);
}

TEST_F(TransportTest, case9) {
  std::string str = "helllo";
  ASSERT_EQ(str.size(), this->transport.send(str.size(), str.c_str()));
  ASSERT_EQ("Content-Length: 6\r\n\r\nhelllo", this->readOutput());
}

struct ServerTest : public InteractiveBase {
  FilePtr logFile;
  int count{0};
  NullLogger clLogger;               // FIXME: record client log?
  std::unique_ptr<Transport> client; // for lazy initialization

  ServerTest() : InteractiveBase("", ""), logFile(createFilePtr(tmpfile)) {
    IOConfig config;
    config.in = IOConfig::PIPE;
    config.out = IOConfig::PIPE;
    config.err = IOConfig::INHERIT;

    this->handle = ProcBuilder::spawn(config, [&]() -> int {
      LSPLogger logger;
      logger.setSeverity(LogLevel::INFO);
      logger.setAppender(std::move(this->logFile));
      LSPServer server(logger, FilePtr(stdin), FilePtr(stdout));
      server.run();
    });

    auto clIn = createFilePtr(fdopen, this->handle.out(), "r");
    auto clOut = createFilePtr(fdopen, this->handle.in(), "w");
    this->client =
        std::make_unique<LSPTransport>(this->clLogger, std::move(clIn), std::move(clOut));
  }

  void call(const char *methodName, JSON &&params) {
    this->client->call(++this->count, methodName, std::move(params));
  }

  void notify(const char *methodName, JSON &&params) const {
    this->client->notify(methodName, std::move(params));
  }

  std::string readLog() const {
    std::string ret = readAfterSeekHead(this->logFile);
    clearFile(this->logFile);
    return ret;
  }

  void callInit() {
    InitializeParams params;

    JSONSerializer serializer;
    serializer(params);
    this->call("initialize", std::move(serializer).take());
    this->expectRegex(".+capabilities.+");
    ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+initialize server.+"));
  }
};

TEST_F(ServerTest, invalid) {
  this->call("hello!!!", {{"de", 34}});
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+server not initialized.+"));
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+must be initialized.+"));

  this->call("hello!!!", "de");
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+Invalid Request.+"));
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid message.+"));

  this->call("initialize", {{"de", 34}});
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+undefined field .+"));
  ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(
      this->readLog(), ::testing::MatchesRegex(".+request message validation failed.+")));
}

TEST_F(ServerTest, init1) { ASSERT_NO_FATAL_FAILURE(this->callInit()); }

TEST_F(ServerTest, init2) {
  ASSERT_NO_FATAL_FAILURE(this->callInit());

  InitializeParams params;

  this->call("initialize", toJSON(params));
  this->expectRegex(".+server has already initialized.+");
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+initialize server.+"));
}

TEST_F(ServerTest, term1) {
  ASSERT_NO_FATAL_FAILURE(this->callInit());

  this->call("shutdown", nullptr);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+result.+null.+"));
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+try to shutdown.+"));

  this->notify("exit", nullptr);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(0, WaitStatus::EXITED));
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+exit server: 0.+"));
}

TEST_F(ServerTest, term2) {
  ASSERT_NO_FATAL_FAILURE(this->callInit());

  this->notify("exit", nullptr);
  ASSERT_NO_FATAL_FAILURE(this->waitAndExpect(1, WaitStatus::EXITED));
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+exit server: 1.+"));
}

TEST_F(ServerTest, term3) {
  this->call("shutdown", nullptr);
  ASSERT_NO_FATAL_FAILURE(this->expectRegex(".+server not initialized.+"));
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+must be initialized.+"));
}

TEST(ClientTest, parse1) {
  auto ret = loadInputScript("hogehoge");
  ASSERT_FALSE(ret);
  ASSERT_EQ("cannot read: hogehoge", ret.asErr());

  TempFileFactory tempFileFactory("ydsh_lsp_client");
  auto fileName = tempFileFactory.createTempFile("script.test", R"(
# this is a comment1 (skip empty section)
---
1234
# this is a comment2
# this is a comment3
---
{ "aaa": true}
# this is a comment4
<<<
<<<
false
)");
  ret = loadInputScript(fileName);
  ASSERT_TRUE(ret);
  ASSERT_EQ(3, ret.asOk().size());
  ASSERT_EQ(1234, ret.asOk()[0].request.asLong());
  ASSERT_FALSE(ret.asOk()[0].waitReply);
  ASSERT_EQ(true, ret.asOk()[1].request["aaa"].asBool());
  ASSERT_TRUE(ret.asOk()[1].waitReply);
  ASSERT_EQ(false, ret.asOk()[2].request.asBool());
  ASSERT_FALSE(ret.asOk()[2].waitReply);
}

TEST(ClientTest, parse2) {
  TempFileFactory tempFileFactory("ydsh_lsp_client");
  auto fileName = tempFileFactory.createTempFile("script.test", R"(
# this is a comment1 (skip empty section)
---
1234
# this is a comment2
# this is a comment3
---
   { true: true}
# this is a comment4 (invalid json)
<<<
<<<
false
)");
  auto ret = loadInputScript(fileName);
  ASSERT_FALSE(ret);

  std::string error = format("%s:8: [error] mismatched token `true', expected `<String>', `}'\n"
                             "   { true: true}\n"
                             "     ^~~~\n",
                             fileName.c_str());
  ASSERT_EQ(error, ret.asErr());
}

TEST(ClientTest, parse3) {
  TempFileFactory tempFileFactory("ydsh_lsp_client");
  auto fileName = tempFileFactory.createTempFile("script.test", R"(
1234
# this is a comment2
# this is a comment3
<<<
false ,
)");
  auto ret = loadInputScript(fileName);
  ASSERT_FALSE(ret);

  std::string error = format("%s:6: [error] mismatched token `,', expected `<EOS>'\n"
                             "false ,\n"
                             "      ^\n",
                             fileName.c_str());
  ASSERT_EQ(error, ret.asErr());
}

TEST(ClientTest, run) {
  TempFileFactory tempFileFactory("ydsh_lsp_client");
  auto fileName = tempFileFactory.createTempFile("script.test", R"(
1234
# this is a comment2
# this is a comment3
<<<
)");
  auto req = loadInputScript(fileName);
  ASSERT_TRUE(req);

  IOConfig config;
  config.in = IOConfig::PIPE;
  config.out = IOConfig::PIPE;
  auto proc = ProcBuilder::spawn(config, [] {
    char buf[1024];
    ssize_t size = read(STDIN_FILENO, buf, std::size(buf));
    if (size > 0) {
      buf[size] = '\0';
      auto res = rpc::Response(1, JSON(buf)).toJSON().serialize();
      std::string content = "Content-Length: ";
      content += std::to_string(res.size());
      content += "\r\n\r\n";
      content += res;
      fwrite(content.c_str(), sizeof(char), content.size(), stdout);
      fflush(stdout);
      return 0;
    }
    return 1;
  });

  ClientLogger logger;
  Client client(logger, createFilePtr(fdopen, proc.out(), "r"),
                createFilePtr(fdopen, proc.in(), "w"));
  rpc::Message ret;
  client.setReplyCallback([&ret](rpc::Message &&msg) -> bool {
    ret = std::move(msg);
    return true;
  });
  client.run(req.asOk());
  auto s = proc.wait();
  ASSERT_TRUE(is<rpc::Response>(ret));
  ASSERT_EQ(0, s.toShellStatus());
  auto &res = get<rpc::Response>(ret);
  ASSERT_EQ(1, res.id.asLong());
  ASSERT_TRUE(res);
  ASSERT_EQ("Content-Length: 4\r\n\r\n1234", res.result.unwrap().asString());
}

struct LocationTest : public ::testing::Test {
  static void checkPosition(const std::string &content, unsigned int pos,
                            const Position &position) {
    {
      auto pos2 = toTokenPos(content, position);
      ASSERT_TRUE(pos2.hasValue());
      ASSERT_EQ(pos, pos2.unwrap());

      auto position2 = toPosition(content, pos2.unwrap());
      ASSERT_TRUE(position2.hasValue());
      ASSERT_EQ(position.toString(), position2.unwrap().toString());
    }

    {
      auto position2 = toPosition(content, pos);
      ASSERT_TRUE(position2.hasValue());
      ASSERT_EQ(position.toString(), position2.unwrap().toString());

      auto pos2 = toTokenPos(content, position2.unwrap());
      ASSERT_TRUE(pos2.hasValue());
      ASSERT_EQ(pos, pos2.unwrap());
    }
  }

  static void checkRange(const std::string &content, Token token, const Range &range) {
    {
      auto token2 = toToken(content, range);
      ASSERT_TRUE(token2.hasValue());
      ASSERT_EQ(token.str(), token2.unwrap().str());

      auto range2 = toRange(content, token2.unwrap());
      ASSERT_TRUE(range2.hasValue());
      ASSERT_EQ(range.toString(), range2.unwrap().toString());
    }

    {
      auto range2 = toRange(content, token);
      ASSERT_TRUE(range2.hasValue());
      ASSERT_EQ(range.toString(), range2.unwrap().toString());

      auto token2 = toToken(content, range2.unwrap());
      ASSERT_TRUE(token2.hasValue());
      ASSERT_EQ(token.str(), token2.unwrap().str());
    }
  }
};

TEST_F(LocationTest, position1) {
  std::string content = "var a = 34;\n"      // 0-11
                        "   $a as String\n"  // 0-15
                        "\n"                 // 0
                        "assert $a == 34\n"; // 0-15
  // check position
  ASSERT_NO_FATAL_FAILURE(checkPosition("", 0, {.line = 0, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition("a", 0, {.line = 0, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition("a", 1, {.line = 0, .character = 1}));
  ASSERT_NO_FATAL_FAILURE(checkPosition("\n", 0, {.line = 0, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(" \n", 0, {.line = 0, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(" \n", 1, {.line = 0, .character = 1}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 0, {.line = 0, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 10, {.line = 0, .character = 10}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 11, {.line = 0, .character = 11}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 12, {.line = 1, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 13, {.line = 1, .character = 1}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 14, {.line = 1, .character = 2}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 14, {.line = 1, .character = 2}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 28, {.line = 2, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 29, {.line = 3, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 43, {.line = 3, .character = 14}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 44, {.line = 3, .character = 15}));

  ASSERT_TRUE(toTokenPos("", {.line = 0, .character = 1}).hasValue());
  ASSERT_EQ(0, toTokenPos("", {.line = 0, .character = 1}).unwrap());

  ASSERT_TRUE(toTokenPos("", {.line = 0, .character = 2}).hasValue());
  ASSERT_EQ(0, toTokenPos("", {.line = 0, .character = 1}).unwrap());

  ASSERT_TRUE(toTokenPos(content, {.line = 3, .character = 16}).hasValue());
  ASSERT_EQ(45, toTokenPos(content, {.line = 3, .character = 16}).unwrap());

  ASSERT_TRUE(toPosition("", 0).hasValue());
  ASSERT_TRUE(toPosition("", 1).hasValue());
  ASSERT_EQ((Position{.line = 0, .character = 0}).toString(),
            toPosition("", 1).unwrap().toString());

  ASSERT_TRUE(toPosition("\n", 1).hasValue());
  ASSERT_EQ((Position{.line = 0, .character = 1}).toString(),
            toPosition("\n", 1).unwrap().toString());

  ASSERT_TRUE(toPosition(content, 45).hasValue());
  ASSERT_EQ((Position{.line = 3, .character = 16}).toString(),
            toPosition(content, 45).unwrap().toString());

  ASSERT_TRUE(toPosition(content, 46).hasValue());
  ASSERT_EQ((Position{.line = 3, .character = 16}).toString(),
            toPosition(content, 46).unwrap().toString());
}

TEST_F(LocationTest, position2) {
  std::string content = "'あい1'\n" // 0-9
                        "'𐐀1'\n";   // 0-6

  // check position
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 1, {.line = 0, .character = 1}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 4, {.line = 0, .character = 2}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 7, {.line = 0, .character = 3}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 8, {.line = 0, .character = 4}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 9, {.line = 0, .character = 5}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 10, {.line = 1, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 11, {.line = 1, .character = 1}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 15, {.line = 1, .character = 3}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 16, {.line = 1, .character = 4}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 17, {.line = 1, .character = 5}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(content, 18, {.line = 1, .character = 6}));
}

TEST_F(LocationTest, range) {
  std::string content = "var a = 34;\n"      // 0-11
                        "   $a as String\n"  // 0-15
                        "\n"                 // 0
                        "assert $a == 34\n"; // 0-15
  // check range
  ASSERT_NO_FATAL_FAILURE(
      checkRange(content, {.pos = 0, .size = 3},
                 Range{.start = {.line = 0, .character = 0}, .end = {.line = 0, .character = 3}}));
  ASSERT_NO_FATAL_FAILURE(
      checkRange(content, {.pos = 15, .size = 2},
                 Range{.start = {.line = 1, .character = 3}, .end = {.line = 1, .character = 5}}));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}