#include "gtest/gtest.h"

#include <arg_parser.h>
#include <vm.h>
#include <ydsh/ydsh.h>

#include "../arg_parser_helper.hpp"

using namespace ydsh;

template <typename... T>
static ObjPtr<ArrayObject> createArgs(T &&...args) {
  std::vector<DSValue> values = {DSValue::createStr(args)...};
  auto v = DSValue::create<ArrayObject>(toUnderlying(TYPE::StringArray), std::move(values));
  return toObjPtr<ArrayObject>(v);
}

static std::string toStringAt(const ArrayObject &obj, size_t index) {
  return obj.getValues()[index].asStrRef().toString();
}

static void fillWithInvalid(BaseObject &obj) {
  for (unsigned int i = 0; i < obj.getFieldSize(); i++) {
    obj[i] = DSValue::createInvalid();
  }
}

class ArgParserTest : public ::testing::Test {
protected:
  DSState *state{nullptr};

public:
  void SetUp() override { this->state = DSState_create(); }

  void TearDown() override { DSState_delete(&this->state); }

  TypePool &typePool() { return this->state->typePool; }

  const CLIRecordType &createRecordType(const char *typeName, ArgEntriesBuilder &&builder) {
    return ::createRecordType(this->typePool(), typeName, std::move(builder), ModId{1});
  }
};

TEST_F(ArgParserTest, base) {
  ArgEntriesBuilder builder;
  builder
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setShortName('s');
        e.setLongName("status");
      })
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::HAS_ARG);
        e.setShortName('o');
        e.setLongName("output");
        e.setArgName("arg");
      })
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setShortName('d');
        e.setDetail("enable debug");
        e.setAttr(ArgEntryAttr::STORE_FALSE);
      })
      .addHelp();

  auto &recordType = this->createRecordType("type1", std::move(builder));
  auto &entries = recordType.getEntries();
  ASSERT_EQ(4, entries.size());
  ASSERT_EQ(OptParseOp::NO_ARG, entries[0].getParseOp());
  ASSERT_EQ('s', entries[0].getShortName());
  ASSERT_EQ("status", entries[0].getLongName());
  ASSERT_EQ(OptParseOp::HAS_ARG, entries[1].getParseOp());
  ASSERT_EQ('o', entries[1].getShortName());
  ASSERT_EQ("output", entries[1].getLongName());
  ASSERT_EQ(OptParseOp::NO_ARG, entries[2].getParseOp());
  ASSERT_EQ('d', entries[2].getShortName());
  ASSERT_TRUE(entries[2].getLongName().empty());

  auto args = createArgs("-s", "--output", "AAA", "-d", "BBB", "CCC");
  ASSERT_EQ(6, args->size());
  ASSERT_EQ("-s", toStringAt(*args, 0));
  ASSERT_EQ("--output", toStringAt(*args, 1));
  ASSERT_EQ("AAA", toStringAt(*args, 2));
  ASSERT_EQ("-d", toStringAt(*args, 3));
  ASSERT_EQ("BBB", toStringAt(*args, 4));
  ASSERT_EQ("CCC", toStringAt(*args, 5));

  auto out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  ASSERT_EQ(4, out->getFieldSize());
  ASSERT_FALSE((*out)[0]);
  ASSERT_FALSE((*out)[1]);
  ASSERT_FALSE((*out)[2]);
  ASSERT_FALSE((*out)[3]);
  (*out)[0] = DSValue::createStr("cmd1");
  bool s = parseArgs(*this->state, *args, *out);
  ASSERT_TRUE(s);
  ASSERT_TRUE((*out)[1].asBool());
  ASSERT_EQ("AAA", (*out)[2].asStrRef().toString());
  ASSERT_FALSE((*out)[3].asBool());

  const char *help = R"(Usage: cmd1 [OPTIONS]

Options:
  -s, --status
  -o, --output arg
  -d                enable debug
  -h, --help        show this help message)";
  std::string v;
  ArgParser::create(recordType.getEntries()).formatUsage("cmd1", true, v);
  ASSERT_EQ(help, v);
}

TEST_F(ArgParserTest, opt) {
  ArgEntriesBuilder builder;
  builder
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::OPT_ARG);
        e.setShortName('d');
        e.setLongName("dump");
        e.setArgName("file");
        e.setDefaultValue("stdout");
      })
      .addHelp()
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setArgName("src");
        e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REQUIRE);
      })
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setArgName("dest");
        e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REMAIN);
      });

  auto &recordType = this->createRecordType("type1", std::move(builder));

  //
  auto args = createArgs("-d", "AAA");
  auto out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd1");
  bool s = parseArgs(*this->state, *args, *out);
  ASSERT_TRUE(s);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ("stdout", (*out)[1].asStrRef().toString());
  ASSERT_EQ("AAA", (*out)[2].asStrRef().toString());
  ASSERT_TRUE((*out)[3].isInvalid());

  //
  args = createArgs("-d/dev/log", "111", "AAA", "BBB", "CCC");
  out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd1");
  s = parseArgs(*this->state, *args, *out);
  ASSERT_TRUE(s);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ("/dev/log", (*out)[1].asStrRef().toString());
  ASSERT_EQ("111", (*out)[2].asStrRef().toString());
  ASSERT_TRUE((*out)[3].isObject());
  ASSERT_TRUE(isa<ArrayObject>((*out)[3].get()));
  {
    auto &array = typeAs<ArrayObject>((*out)[3]);
    ASSERT_EQ(3, array.size());
    ASSERT_EQ("AAA", toStringAt(array, 0));
    ASSERT_EQ("BBB", toStringAt(array, 1));
    ASSERT_EQ("CCC", toStringAt(array, 2));
  }

  const char *help = R"(Usage: cmd1 [OPTIONS] src [dest...]

Options:
  -d[file], --dump[=file]
  -h, --help               show this help message)";
  std::string v;
  ArgParser::create(recordType.getEntries()).formatUsage("cmd1", true, v);
  ASSERT_EQ(help, v);
}

TEST_F(ArgParserTest, range) {
  ArgEntriesBuilder builder;
  builder
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::HAS_ARG);
        e.setShortName('t');
        e.setLongName("time");
        e.setArgName("msec");
        e.setIntRange(0, 1000);
        e.setAttr(ArgEntryAttr::REQUIRE);
      })
      .addHelp()
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setArgName("level");
        e.setChoice({strdup("info"), strdup("warn")});
        e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REQUIRE);
      });

  auto &recordType = this->createRecordType("type1", std::move(builder));

  //
  auto out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd1");
  auto args = createArgs("--time=1000", "info");
  bool s = parseArgs(*this->state, *args, *out);
  ASSERT_TRUE(s);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(1000, (*out)[1].asInt());
  ASSERT_EQ("info", (*out)[2].asStrRef().toString());

  // validation error (num format)
  out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd1");
  args = createArgs("-t", "qq", "AAA");
  s = parseArgs(*this->state, *args, *out);
  ASSERT_FALSE(s);
  auto error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  const char *err = R"(invalid argument: `qq', must be decimal integer
Usage: cmd1 [OPTIONS] level)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // validation error (int range)
  out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd1");
  args = createArgs("-t", "0", "--time=1001", "AAA");
  s = parseArgs(*this->state, *args, *out);
  ASSERT_FALSE(s);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(0, (*out)[1].asInt());
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  err = R"(invalid argument: `1001', must be [0, 1000]
Usage: cmd1 [OPTIONS] level)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // validation error (choice)
  out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd1");
  args = createArgs("-t", "0", "--time=1000", "Info");
  s = parseArgs(*this->state, *args, *out);
  ASSERT_FALSE(s);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(1000, (*out)[1].asInt());
  error = state->getCallStack().takeThrownObject();

  err = R"(invalid argument: `Info', must be {info, warn}
Usage: cmd1 [OPTIONS] level)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // missing required options
  out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd2");
  args = createArgs("Info");
  s = parseArgs(*this->state, *args, *out);
  ASSERT_FALSE(s);
  ASSERT_EQ("cmd2", (*out)[0].asStrRef().toString());
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  err = R"(require -t or --time option)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // missing require arguments
  out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd1");
  args = createArgs("-t", "009");
  s = parseArgs(*this->state, *args, *out);
  ASSERT_FALSE(s);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(9, (*out)[1].asInt());
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  err = R"(require `level' argument)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());
}

TEST_F(ArgParserTest, help) {
  ArgEntriesBuilder builder;
  builder.addHelp().add([](ArgEntry &e) {
    e.setParseOp(OptParseOp::NO_ARG);
    e.setArgName("output");
    e.setAttr(ArgEntryAttr::POSITIONAL);
  });

  auto &recordType = this->createRecordType("type1", std::move(builder));

  //
  auto out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd11");
  auto args = createArgs("-h", "AAA");
  bool s = parseArgs(*this->state, *args, *out);
  ASSERT_FALSE(s);
  auto error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(0, error->getStatus());

  const char *err = R"(Usage: cmd11 [output]

Options:
  -h, --help  show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // help with invalid options
  out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  fillWithInvalid(*out);
  (*out)[0] = DSValue::createStr("cmd11");
  args = createArgs("-h", "-A");
  s = parseArgs(*this->state, *args, *out);
  ASSERT_FALSE(s);
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(2, error->getStatus());

  err = R"(invalid option: -A
Usage: cmd11 [output])";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}