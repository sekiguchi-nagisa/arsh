#include "gtest/gtest.h"
#include "../test_common.h"

#include "lsp.h"
#include "server.h"

using namespace ydsh;
using namespace lsp;
using namespace json;
using namespace rpc;

TEST(LSPTest, Position) {
    const char *text = R"(
    {
        "line" : 23, "character" : 34
    }
)";
    auto json = JSON::fromString(text);
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(json.isInvalid()));
    auto line = json.serialize();

    Position pos;
    fromJSON(std::move(json), pos);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(23, pos.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(34, pos.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line, toJSON(pos).serialize()));
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
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(json.isInvalid()));
    auto line = json.serialize();

    Range range;
    fromJSON(std::move(json), range);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(90, range.start.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(100, range.start.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(100, range.end.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(24, range.end.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line, toJSON(range).serialize()));
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
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(json.isInvalid()));
    auto line = json.serialize();

    Location location;
    fromJSON(std::move(json), location);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("/hoge/hoge", location.uri.uri));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(90, location.range.start.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(15, location.range.start.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(100, location.range.end.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(24, location.range.end.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line, toJSON(location).serialize()));
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
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(json.isInvalid()));
    auto line = json.serialize();

    LocationLink link;
    fromJSON(std::move(json), link);
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(link.originSelectionRange.hasValue()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(link.targetSelectionRange.hasValue()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("/hoge", link.targetUri));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(90, link.targetRange.start.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(15, link.targetRange.start.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(100, link.targetRange.end.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(24, link.targetRange.end.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line, toJSON(link).serialize()));
}

TEST(LSPTest, Command) {
    const char *text = R"(
    {
        "title" : "stop",
        "command" : "/stop"
    }
)";
    auto json = JSON::fromString(text);
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(json.isInvalid()));
    auto line = json.serialize();

    Command cmd;
    fromJSON(std::move(json), cmd);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("stop", cmd.title));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("/stop", cmd.command));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line, toJSON(cmd).serialize()));
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
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(json.isInvalid()));
    auto line = json.serialize();

    TextEdit edit;
    fromJSON(std::move(json), edit);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello world!!", edit.newText));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(90, edit.range.start.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(15, edit.range.start.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(100, edit.range.end.line));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(24, edit.range.end.character));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(line, toJSON(edit).serialize()));
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

struct TransportTest : public ::testing::Test {
    LSPLogger logger;
    LSPTransport transport;

    TransportTest() : transport(this->logger, createFilePtr(tmpfile), createFilePtr(tmpfile)) {
        this->logger.setSeverity(LogLevel::INFO);
        this->logger.setAppender(createFilePtr(tmpfile));
    }

    void setInput(const std::string &str) {
        writeAndSeekToHead(this->transport.getInput(), str);
    }

    std::string readLog() const {
        return readAfterSeekHead(this->logger.getAppender());
    }

    std::string readOutput() {
        return readAfterSeekHead(this->transport.getOutput());
    }
};

TEST_F(TransportTest, case1) {
    this->setInput("hoge");
    int size = this->transport.recvSize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, size));
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: hoge.+")));
}

TEST_F(TransportTest, case2) {
    this->setInput("hoge\r");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, this->transport.recvSize()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: hoge.+")));
}

TEST_F(TransportTest, case3) {
    this->setInput("hoge\n");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, this->transport.recvSize()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+invalid header: hoge.+")));
}

TEST_F(TransportTest, case4) {
    this->setInput("hoge: 34\r\n");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, this->transport.recvSize()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+other header: hoge: 34.+")));
}

TEST_F(TransportTest, case5) {
    this->setInput("Content-Length: hey\r\n");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, this->transport.recvSize()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+may be broken content length.+")));
}

TEST_F(TransportTest, case6) {
    this->setInput("Content-Length: 12\r\nContent-Length: 5600\r\n");
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, this->transport.recvSize()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(this->readLog(), ::testing::MatchesRegex(".+previous read message length: 12.+")));
}

TEST_F(TransportTest, case7) {
    this->setInput("Content-Length: 12\r\nContent-Length: 5\r\n\r\n12345");
    int size = this->transport.recvSize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5, size));
    auto logStr = this->readLog();
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(logStr, ::testing::MatchesRegex(".+Content-Length: 5.+")));

    char data[5];
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5, this->transport.recv(arraySize(data), data)));
    std::string str(data, 5);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("12345", str));
}

TEST_F(TransportTest, case8) {
    this->setInput("Content-Length: 5\r\n\r\n12345");
    int size = this->transport.recvSize();
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(5, size));
    auto logStr = this->readLog();
    ASSERT_NO_FATAL_FAILURE(ASSERT_THAT(logStr, ::testing::MatchesRegex(".+Content-Length: 5.+")));

    char data[3];
    int recvSize = this->transport.recv(arraySize(data), data);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3, recvSize));
    std::string str(data, recvSize);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("123", str));

    recvSize = this->transport.recv(arraySize(data), data);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, recvSize));
    str = std::string(data, recvSize);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("45", str));
}

TEST_F(TransportTest, case9) {
    std::string str = "helllo";
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(str.size(), this->transport.send(str.size(), str.c_str())));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("Content-Length: 6\r\n\r\nhelllo", this->readOutput()));
}


struct ServerTest : public InteractiveBase {
    ServerTest() : InteractiveBase("", "", false) {
        IOConfig config;
        config.in = IOConfig::PIPE;
        config.out = IOConfig::PIPE;
        config.err = IOConfig::PIPE;

        this->handle = ProcBuilder::spawn(config, []() -> int {
            auto in = createFilePtr(fdopen, STDIN_FILENO, "r");
            auto out = createFilePtr(fdopen, STDOUT_FILENO, "w");

            LSPLogger logger;
            LSPServer server(std::move(in), std::move(out), logger);
            server.run();
        });
    }
};

//TEST_F(ServerTest, invalid) {
//
//}

//TEST_F(ServerTest, init) {
//    InitializeParams params;
//    params.processId = 100;
//    params.rootUri = nullptr;
//
//
//}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}