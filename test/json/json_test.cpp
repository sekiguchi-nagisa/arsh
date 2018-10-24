#include "gtest/gtest.h"

#include <json.h>

using namespace json;

TEST(JSON, type) {
    JSON json;
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isNull()));

    json = true;
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isBool()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, json.asBool()));

    json = JSON(12);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(12, json.asLong()));

    json = JSON(3.14);
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isDouble()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3.14, json.asDouble()));

    json = JSON("hello");
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", json.asString()));

    json = JSON(array());
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isArray()));
    json.asArray().push_back(JSON(23));
    json.asArray().push_back(true);
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, json.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(23, json[0].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json[1].asBool()));

    json = array(
           23, 4.3, "hello", false, nullptr, array(34, 34), std::move(json)
    );
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isArray()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(7, json.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(23, json[0].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4.3, json[1].asDouble()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", json[2].asString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(false, json[3].asBool()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json[4].isNull()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(34, json[5][0].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(34, json[5][1].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(23, json[6][0].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, json[6][1].asBool()));

    json = JSON(createObject());
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isObject()));
    json.asObject().emplace("hey", false);
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(json["hey"].asBool()));

    json = {
            {"hello", 45}, {"world", array("false", true)}
    };
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json.isObject()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(2, json.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(45, json["hello"].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("false", json["world"][0].asString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(json["world"][1].asBool()));
}

class ParserTest : public ::testing::Test {
protected:
    JSON ret;

    void parse(const char *src) {
        SCOPED_TRACE("");

        Parser parser(src);
        this->ret = parser();
        if(parser.hasError()) {
            parser.showError();
        }
        ASSERT_FALSE(parser.hasError());
    }

public:
    ParserTest() = default;
};

TEST_F(ParserTest, null1) {
    ASSERT_NO_FATAL_FAILURE(this->parse("null"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isNull()));
}

TEST_F(ParserTest, null2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("\t\n\tnull  \t   \n\n  \n"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isNull()));
}

TEST_F(ParserTest, bool1) {
    ASSERT_NO_FATAL_FAILURE(this->parse("\t\n\t false  \t   \n\n  \n"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isBool()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_FALSE(this->ret.asBool()));
}

TEST_F(ParserTest, bool2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("true"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isBool()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.asBool()));
}

TEST_F(ParserTest, num1) {
    ASSERT_NO_FATAL_FAILURE(this->parse("\t\n\t9876  \t   \n\n  \n"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(9876, this->ret.asLong()));
}

TEST_F(ParserTest, num2) {
    ASSERT_NO_FATAL_FAILURE(this->parse("\t\n\t-1203  \t   \n\n  \n"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1203, this->ret.asLong()));
}

TEST_F(ParserTest, num3) {
    ASSERT_NO_FATAL_FAILURE(this->parse("\t\n\t-1203e+2  \t   \n\n  \n"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isDouble()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-120300, this->ret.asDouble()));
}

TEST_F(ParserTest, num4) {
    ASSERT_NO_FATAL_FAILURE(this->parse("\t\n\t12.03E-2  \t   \n\n  \n"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isDouble()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0.1203, this->ret.asDouble()));
}

TEST_F(ParserTest, num5) {
    ASSERT_NO_FATAL_FAILURE(this->parse("\t 0 \n"));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(0, this->ret.asLong()));
}

TEST_F(ParserTest, string1) {
    const char *text = R"("hello")";
    ASSERT_NO_FATAL_FAILURE(this->parse(text));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("hello", this->ret.asString()));
}

template <unsigned int N>
static std::string str(const char (&v)[N]) {
    return std::string(v, N - 1);
}

TEST_F(ParserTest, string2) {
    const char *text = R"("hello\n\t\b\f\r \u0023\u0000\ud867\uDE3d\u0026")";
    ASSERT_NO_FATAL_FAILURE(this->parse(text));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isString()));

    ASSERT_NO_FATAL_FAILURE(
            ASSERT_EQ(str("hello\n\t\b\f\r #\0\xf0\xa9\xb8\xbd&"), this->ret.asString()));
}

TEST_F(ParserTest, string3) {
    const char *text = R"("\"\\\/")";
    ASSERT_NO_FATAL_FAILURE(this->parse(text));

    const char *expect = R"("\/)";
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect, this->ret.asString()));
}

TEST_F(ParserTest, array) {
    const char *text = R"(
        [
           "a" , "b", true
               , null,
              [45
                ,
               54
             ], 3.14
        ])";
    ASSERT_NO_FATAL_FAILURE(this->parse(text));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isArray()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(6, this->ret.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("a", this->ret[0].asString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ("b", this->ret[1].asString()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, this->ret[2].asBool()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, this->ret[3].isNull()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(45, this->ret[4][0].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(54, this->ret[4][1].asLong()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(3.14, this->ret[5].asDouble()));
}

TEST_F(ParserTest, object) {
    const char *text = R"(
     { "23" :
         [true, false, null], "aaa"
          : null
          ,
           "bbb" : {
                   }
           ,
              "ccc" :[]
        }
)";
    ASSERT_NO_FATAL_FAILURE(this->parse(text));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret.isObject()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(4, this->ret.size()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, this->ret["23"][0].asBool()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(false, this->ret["23"][1].asBool()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, this->ret["23"][2].isNull()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, this->ret["aaa"].isNull()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, this->ret["bbb"].asObject().empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(true, this->ret["ccc"].asArray().empty()));
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(this->ret["hohgeo"].isNull()));
}

TEST_F(ParserTest, serialize) {
    auto expect = JSON {
            {"34", {}},
            {"aa", false},
            {"bb", array(nullptr, 3, 3.4, "hey", JSON { {"huga", array()} })},
            {"ZZ", {
                           {";;;", 234}, {"", {}}
            }}
    }.serialize(3);

    auto actual = Parser(expect.c_str(), expect.size())().serialize(3);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect, actual));

    expect = JSON(34).serialize(3);
    actual = Parser(expect.c_str(), expect.size())().serialize(3);

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expect, actual));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}