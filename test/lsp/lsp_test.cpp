#include "gtest/gtest.h"

#include "lsp.h"

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


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}