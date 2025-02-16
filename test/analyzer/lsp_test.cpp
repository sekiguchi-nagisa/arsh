#include "../test_common.h"

#include "client.h"
#include "lsp.h"
#include "server.h"
#include "worker.h"

using namespace arsh;
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
  ASSERT_EQ("23:34", pos.toString());
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
  ASSERT_EQ("(90:100~100:24)", range.toString());
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

static void writeAndSeekToHead(int fd, const std::string &line) {
  ssize_t s = write(fd, line.c_str(), line.size());
  (void)s;
  fsync(fd);
  lseek(fd, 0L, SEEK_SET);
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

  TransportTest()
      : transport(this->logger, dupFD(fileno(createFilePtr(tmpfile).get())),
                  dupFD(fileno(createFilePtr(tmpfile).get()))) {
    this->logger.setSeverity(LogLevel::DEBUG);
    this->logger.setAppender(createFilePtr(tmpfile));
  }

  void setInput(const std::string &str) const {
    writeAndSeekToHead(this->transport.getInputFd(), str);
  }

  std::string readLog() const { return readAfterSeekHead(this->logger.getAppender()); }
};

TEST_F(TransportTest, case1) {
  this->setInput("hoge");
  ssize_t size = this->transport.recvSize();
  ASSERT_EQ(-1, size);
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: `hoge'.+"));
}

TEST_F(TransportTest, case2) {
  this->setInput("hoge\r");
  ASSERT_EQ(-1, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: `hoge.+"));
}

TEST_F(TransportTest, case3) {
  this->setInput("hoge\n");
  ASSERT_EQ(-1, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: `hoge.+"));
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
  ssize_t size = this->transport.recvSize();
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
  ssize_t size = this->transport.recvSize();
  ASSERT_EQ(5, size);
  auto logStr = this->readLog();
  ASSERT_THAT(logStr, ::testing::MatchesRegex(".+Content-Length: 5.+"));

  char data[3];
  ssize_t recvSize = this->transport.recv(std::size(data), data);
  ASSERT_EQ(3, recvSize);
  std::string str(data, recvSize);
  ASSERT_EQ("123", str);

  recvSize = this->transport.recv(std::size(data), data);
  ASSERT_EQ(2, recvSize);
  str = std::string(data, recvSize);
  ASSERT_EQ("45", str);
}

TEST_F(TransportTest, case9) {
  this->setInput("Content-Length: 0\r\n\r\n");
  ASSERT_EQ(0, this->transport.recvSize());
  ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+Content-Length: 0.+"));
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
      LSPServer server(logger, dupFD(STDIN_FILENO), dupFD(STDOUT_FILENO), 100, "");
      server.run();
      return 1;
    });

    auto clIn = dupFD(this->handle.out());
    auto clOut = dupFD(this->handle.in());
    this->client = std::make_unique<LSPTransport>(this->clLogger, clIn, clOut);
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

  TempFileFactory tempFileFactory("arsh_lsp_client");
  auto fileName = tempFileFactory.createTempFile("script.test", R"(
# this is a comment1 (skip empty section)
---
1234
# this is a comment2
# this is a comment3
---
{ "aaa": true}
# this is a comment4
<<< 12345
<<<
false
)");
  ret = loadInputScript(fileName);
  ASSERT_TRUE(ret);
  auto &req = ret.asOk().req;
  ASSERT_EQ(3, req.size());
  ASSERT_EQ(1234, req[0].request.asLong());
  ASSERT_EQ(0, req[0].msec);
  ASSERT_EQ(true, req[1].request["aaa"].asBool());
  ASSERT_EQ(12345, req[1].msec);
  ASSERT_EQ(false, req[2].request.asBool());
  ASSERT_EQ(0, req[2].msec);
}

TEST(ClientTest, parse2) {
  TempFileFactory tempFileFactory("arsh_lsp_client");
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
  TempFileFactory tempFileFactory("arsh_lsp_client");
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

TEST(ClientTest, parse4) {
  TempFileFactory tempFileFactory("arsh_lsp_client");
  auto fileName = tempFileFactory.createTempFile("script.test", R"(
1234
# this is a comment2
# this is a comment3
<<< hoge
false ,
)");
  auto ret = loadInputScript(fileName);
  ASSERT_FALSE(ret);

  std::string error = format("%s:5: [error] invalid token, expected `<EOS>'\n"
                             "<<< hoge\n"
                             "^\n",
                             fileName.c_str());
  ASSERT_EQ(error, ret.asErr());
}

TEST(ClientTest, run) {
  TempFileFactory tempFileFactory("arsh_lsp_client");
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
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      return 0;
    }
    return 1;
  });

  ClientLogger logger;
  logger.setSeverity(LogLevel::DEBUG);
  Client client(logger, dupFD(proc.out()), dupFD(proc.in()));
  rpc::Message ret;
  client.setReplyCallback([&ret](rpc::Message &&msg) -> bool {
    if (is<rpc::Error>(msg) && Client::isBrokenOrEmpty(get<rpc::Error>(msg))) {
      return false;
    }
    ret = std::move(msg);
    return true;
  });
  client.run(req.asOk());
  auto s = proc.wait();
  ASSERT_EQ(0, s.toShellStatus());
  ASSERT_TRUE(is<rpc::Response>(ret));
  auto &res = get<rpc::Response>(ret);
  ASSERT_EQ(1, res.id.asLong());
  ASSERT_TRUE(res);
  ASSERT_EQ("Content-Length: 4\r\n\r\n1234", res.result.unwrap().asString());
}

static Source source(StringRef content) {
  return {"dummy", BUILTIN_MOD_ID, content.toString(), 0, {}};
}

struct LocationTest : public ::testing::Test {
  static void checkPosition(const std::string &content, unsigned int pos,
                            const Position &position) {
    auto src = source(content);

    {
      auto pos2 = toTokenPos(src.getContent(), position);
      ASSERT_TRUE(pos2.hasValue());
      ASSERT_EQ(pos, pos2.unwrap());

      auto position2 = src.toPosition(pos2.unwrap());
      ASSERT_TRUE(position2.hasValue());
      ASSERT_EQ(position.toString(), position2.unwrap().toString());
    }

    {
      auto position2 = src.toPosition(pos);
      ASSERT_TRUE(position2.hasValue());
      ASSERT_EQ(position.toString(), position2.unwrap().toString());

      auto pos2 = toTokenPos(src.getContent(), position2.unwrap());
      ASSERT_TRUE(pos2.hasValue());
      ASSERT_EQ(pos, pos2.unwrap());
    }
  }

  static void checkRange(const std::string &content, Token token, const Range &range) {
    auto src = source(content);

    {
      auto token2 = toToken(src.getContent(), range);
      ASSERT_TRUE(token2.hasValue());
      ASSERT_EQ(token.str(), token2.unwrap().str());

      auto range2 = src.toRange(token2.unwrap());
      ASSERT_TRUE(range2.hasValue());
      ASSERT_EQ(range.toString(), range2.unwrap().toString());
    }

    {
      auto range2 = src.toRange(token);
      ASSERT_TRUE(range2.hasValue());
      ASSERT_EQ(range.toString(), range2.unwrap().toString());

      auto token2 = toToken(src.getContent(), range2.unwrap());
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
  ASSERT_NO_FATAL_FAILURE(checkPosition("\n", 0, {.line = 0, .character = 0}));
  ASSERT_NO_FATAL_FAILURE(checkPosition(" \n", 0, {.line = 0, .character = 0}));
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

  ASSERT_TRUE(source("").toPosition(0).hasValue());
  ASSERT_TRUE(source("").toPosition(1).hasValue());
  ASSERT_EQ((Position{.line = 0, .character = 0}).toString(),
            source("").toPosition(1).unwrap().toString());

  ASSERT_TRUE(source("a").toPosition(1).hasValue());
  ASSERT_EQ((Position{.line = 0, .character = 1}).toString(),
            source("a").toPosition(1).unwrap().toString());

  ASSERT_TRUE(source("\n").toPosition(1).hasValue());
  ASSERT_EQ((Position{.line = 0, .character = 0}).toString(),
            source("\n").toPosition(1).unwrap().toString());

  ASSERT_TRUE(source(" \n").toPosition(1).hasValue());
  ASSERT_EQ((Position{.line = 0, .character = 1}).toString(),
            source(" \n").toPosition(1).unwrap().toString());

  ASSERT_TRUE(source(content).toPosition(45).hasValue());
  ASSERT_EQ((Position{.line = 3, .character = 15}).toString(),
            source(content).toPosition(45).unwrap().toString());

  ASSERT_TRUE(source(content).toPosition(46).hasValue());
  ASSERT_EQ((Position{.line = 3, .character = 15}).toString(),
            source(content).toPosition(46).unwrap().toString());
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

  ASSERT_TRUE(source(content).toPosition(18).hasValue());
  ASSERT_EQ((Position{.line = 1, .character = 5}).toString(),
            source(content).toPosition(18).unwrap().toString());
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

TEST_F(LocationTest, change) {
  std::string content;

  TextDocumentContentChangeEvent change = {
      .range = Range{.start = {.line = 0, .character = 0}, .end = {.line = 0, .character = 0}},
      .rangeLength = 0,
      .text = "1234",
  };
  ASSERT_TRUE(applyChange(content, change));
  ASSERT_EQ("1234", content);

  change = {
      .range = Range{.start = {.line = 0, .character = 4}, .end = {.line = 0, .character = 4}},
      .rangeLength = 0,
      .text = "\n",
  };
  ASSERT_TRUE(applyChange(content, change));
  ASSERT_EQ("1234\n", content);

  change = {
      .range = Range{.start = {.line = 1, .character = 0}, .end = {.line = 1, .character = 0}},
      .rangeLength = 0,
      .text = "あ",
  };
  ASSERT_TRUE(applyChange(content, change));
  ASSERT_EQ("1234\nあ", content);

  change = {
      .range = Range{.start = {.line = 1, .character = 1}, .end = {.line = 1, .character = 1}},
      .rangeLength = 0,
      .text = "1",
  };
  ASSERT_TRUE(applyChange(content, change));
  ASSERT_EQ("1234\nあ1", content);

  change = {
      .range = Range{.start = {.line = 1, .character = 2}, .end = {.line = 1, .character = 2}},
      .rangeLength = 0,
      .text = "う",
  };
  ASSERT_TRUE(applyChange(content, change));
  ASSERT_EQ("1234\nあ1う", content);

  change = {
      .range = Range{.start = {.line = 0, .character = 2}, .end = {.line = 1, .character = 3}},
      .rangeLength = 6,
      .text = "-",
  };
  ASSERT_TRUE(applyChange(content, change));
  ASSERT_EQ("12-", content);
}

struct SemanticTokenTest : public ::testing::Test {
  SemanticTokenEncoder encoder;

  SemanticTokenTest() : encoder(SemanticTokensLegend::create()) {}

  void testEncode(HighlightTokenClass tokenClass, SemanticTokenTypes type,
                  unsigned int modifiers) const {
    auto ret = this->encoder.encode(tokenClass);
    ASSERT_TRUE(ret.hasValue());
    ASSERT_EQ(static_cast<unsigned int>(type), ret.unwrap().first);
    ASSERT_EQ(modifiers, ret.unwrap().second);
  }
};

TEST_F(SemanticTokenTest, split) {
  // multi line
  const char *src = R"("aaa
bbb
ccc"
)";
  StringRef ref = src;
  Token token = {
      .pos = 0,
      .size = static_cast<unsigned int>(ref.size() - 1),
  };
  std::vector<std::string> values;
  splitTokenByNewline(ref, token, [&](Token sub) {
    auto value = ref.substr(sub.pos, sub.size).toString();
    values.push_back(std::move(value));
  });
  ASSERT_EQ(3, values.size());
  ASSERT_EQ("\"aaa", values[0]);
  ASSERT_EQ("bbb", values[1]);
  ASSERT_EQ("ccc\"", values[2]);

  // single line
  ref = "abc  \n";
  token = {
      .pos = 0,
      .size = 3,
  };
  values.clear();
  splitTokenByNewline(ref, token, [&](Token sub) {
    auto value = ref.substr(sub.pos, sub.size).toString();
    values.push_back(std::move(value));
  });
  ASSERT_EQ(1, values.size());
  ASSERT_EQ("abc", values[0]);
}

TEST_F(SemanticTokenTest, encode) {
  auto ret = this->encoder.encode(HighlightTokenClass::NONE_);
  ASSERT_FALSE(ret.hasValue());

  this->testEncode(HighlightTokenClass::COMMENT, SemanticTokenTypes::comment_, 0);
  this->testEncode(HighlightTokenClass::KEYWORD, SemanticTokenTypes::keyword_, 0);
  this->testEncode(HighlightTokenClass::OPERATOR, SemanticTokenTypes::operator_, 0);
  this->testEncode(HighlightTokenClass::NUMBER, SemanticTokenTypes::number_, 0);
  this->testEncode(HighlightTokenClass::REGEX, SemanticTokenTypes::regexp_, 0);
  this->testEncode(HighlightTokenClass::STRING, SemanticTokenTypes::string_, 0);
  this->testEncode(HighlightTokenClass::COMMAND, SemanticTokenTypes::function_, 0);
  this->testEncode(HighlightTokenClass::COMMAND_ARG, SemanticTokenTypes::commandArgument_, 0);
  this->testEncode(HighlightTokenClass::REDIRECT, SemanticTokenTypes::operator_, 0);
  this->testEncode(HighlightTokenClass::VARIABLE, SemanticTokenTypes::variable_, 0);
  this->testEncode(HighlightTokenClass::TYPE, SemanticTokenTypes::type_, 0);
  this->testEncode(HighlightTokenClass::MEMBER, SemanticTokenTypes::property_, 0);
}

TEST(WorkerTest, base) {
  SingleBackgroundWorker worker;
  auto ret1 = worker.addTask([] {
    std::string value;
    for (unsigned int i = 0; i < 10; i++) {
      if (!value.empty()) {
        value += "_";
      }
      value += std::to_string(i);
    }
    return value;
  });

  auto ret2 = worker.addTask(
      [](unsigned int offset) {
        std::string value;
        for (unsigned int i = offset; i < 20; i++) {
          if (!value.empty()) {
            value += ".";
          }
          value += std::to_string(i);
        }
        return value;
      },
      10);

  ASSERT_EQ("10.11.12.13.14.15.16.17.18.19", ret2.get());
  ASSERT_EQ("0_1_2_3_4_5_6_7_8_9", ret1.get());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}