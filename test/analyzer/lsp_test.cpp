#include "../test_common.h"
#include "gtest/gtest.h"

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

TEST(ASTCtxTest, base) {
  auto ctx = std::make_unique<ASTContext>(1, uri::URI::fromString("file:///file"), "#hello", 1);
  auto *handle = ctx->getScope()->find("COMP_HOOK");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(hasFlag(handle->attr(), FieldAttribute::GLOBAL));
  ASSERT_EQ(0, handle->getModID());
  handle = ctx->getScope()->find("TRUE");
  ASSERT_TRUE(handle);
  ASSERT_TRUE(hasFlag(handle->attr(), FieldAttribute::GLOBAL | FieldAttribute::READ_ONLY));
  ASSERT_EQ(0, handle->getModID());
  ASSERT_TRUE(ctx->getPool().getDiscardPoint().typeIdOffset <= UINT8_MAX);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}