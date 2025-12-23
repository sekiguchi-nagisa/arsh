#include "gtest/gtest.h"

#include <arg_parser.h>
#include <arsh/arsh.h>
#include <vm.h>

#include "../arg_parser_helper.hpp"

using namespace arsh;

template <typename... T>
static ObjPtr<ArrayObject> createArgs(T &&...args) {
  std::vector<Value> values = {Value::createStr(args)...};
  return createObject<ArrayObject>(toUnderlying(TYPE::StringArray), std::move(values));
}

static std::string toStringAt(const ArrayObject &obj, size_t index) {
  return obj[index].asStrRef().toString();
}

static void fillWithInvalid(BaseObject &obj) {
  for (unsigned int i = 0; i < obj.getFieldSize(); i++) {
    obj[i] = Value::createInvalid();
  }
}

class ArgParserTest : public ::testing::Test {
protected:
  ARState *state{nullptr};

public:
  void SetUp() override { this->state = ARState_create(); }

  void TearDown() override { ARState_delete(&this->state); }

  TypePool &typePool() { return this->state->typePool; }

  const CLIRecordType &createRecordType(const char *typeName, ArgEntriesBuilder &&builder,
                                        CLIRecordType::Attr attr = CLIRecordType::Attr::VERBOSE,
                                        const char *desc = "") {
    return ::createRecordType(this->typePool(), typeName, std::move(builder), ModId{1}, attr, desc);
  }
};

TEST_F(ArgParserTest, base) {
  ArgEntriesBuilder builder;
  builder
      .add(this->typePool().get(TYPE::Bool),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::NO_ARG);
             e.setShortName('s');
             e.setLongName("status");
           })
      .add(this->typePool().get(TYPE::String),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::HAS_ARG);
             e.setShortName('o');
             e.setLongName("output");
             e.setArgName("arg");
           })
      .add(this->typePool().get(TYPE::Bool),
           [](ArgEntry &e) {
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

  auto out = createObject<BaseObject>(recordType);
  ASSERT_EQ(4, out->getFieldSize());
  ASSERT_FALSE((*out)[0]);
  ASSERT_FALSE((*out)[1]);
  ASSERT_FALSE((*out)[2]);
  ASSERT_FALSE((*out)[3]);
  (*out)[0] = Value::createStr("cmd1");
  auto ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_TRUE(ret);
  ASSERT_EQ(4, ret.index);
  ASSERT_TRUE((*out)[1].asBool());
  ASSERT_EQ("AAA", (*out)[2].asStrRef().toString());
  ASSERT_FALSE((*out)[3].asBool());

  const char *help = R"(Usage: cmd1 [OPTIONS]

Options:
  -s, --status
  -o, --output arg
  -d                enable debug
  -h, --help        show this help message)";
  std::string v = createArgParser("cmd1", recordType).formatUsage("", true).unwrap();
  ASSERT_EQ(help, v);
}

TEST_F(ArgParserTest, opt) {
  ArgEntriesBuilder builder;
  builder
      .add(this->typePool().get(TYPE::String),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::OPT_ARG);
             e.setShortName('d');
             e.setLongName("dump");
             e.setArgName("file");
             e.setDefaultValue("stdout");
           })
      .addHelp()
      .add(this->typePool().get(TYPE::String),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::NO_ARG);
             e.setArgName("src");
             e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REQUIRED);
           })
      .add(this->typePool().get(TYPE::StringArray), [](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setArgName("dest");
        e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REMAIN);
      });

  auto &recordType = this->createRecordType("type1", std::move(builder));

  //
  auto args = createArgs("-d", "AAA");
  auto out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  auto ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_TRUE(ret);
  ASSERT_EQ(2, ret.index);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ("stdout", (*out)[1].asStrRef().toString());
  ASSERT_EQ("AAA", (*out)[2].asStrRef().toString());
  ASSERT_TRUE((*out)[3].isInvalid());

  //
  args = createArgs("-d/dev/log", "111", "AAA", "BBB", "CCC");
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_TRUE(ret);
  ASSERT_EQ(5, ret.index);
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
  std::string v = createArgParser("cmd1", recordType).formatUsage("", true).unwrap();
  ASSERT_EQ(help, v);
}

TEST_F(ArgParserTest, stop) {
  ArgEntriesBuilder builder;
  builder
      .add(this->typePool().get(TYPE::String),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::HAS_ARG);
             e.setShortName('c');
             e.setArgName("cmd");
             e.setAttr(ArgEntryAttr::STOP_OPTION);
           })
      .add(this->typePool().get(TYPE::Bool),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::NO_ARG);
             e.setShortName('d');
           })
      .addHelp()
      .add(this->typePool().get(TYPE::StringArray), [](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setArgName("remain");
        e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REMAIN);
      });

  auto &recordType = this->createRecordType("type1", std::move(builder));

  //
  auto out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  auto args = createArgs("-c", "invoke", "-d", "-h", "-2");
  auto ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_TRUE(ret);
  ASSERT_EQ(5, ret.index);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ("invoke", (*out)[1].asStrRef().toString());
  ASSERT_TRUE((*out)[2].isInvalid());
  ASSERT_TRUE((*out)[3].isObject());
  ASSERT_TRUE(isa<ArrayObject>((*out)[3].get()));
  {
    auto &array = typeAs<ArrayObject>((*out)[3]);
    ASSERT_EQ(3, array.size());
    ASSERT_EQ("-d", toStringAt(array, 0));
    ASSERT_EQ("-h", toStringAt(array, 1));
    ASSERT_EQ("-2", toStringAt(array, 2));
  }
}

TEST_F(ArgParserTest, range) {
  ArgEntriesBuilder builder;
  builder
      .add(this->typePool().get(TYPE::Int),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::HAS_ARG);
             e.setShortName('t');
             e.setLongName("time");
             e.setArgName("msec");
             e.setIntRange(0, 1000);
             e.setAttr(ArgEntryAttr::REQUIRED);
           })
      .addHelp()
      .add(this->typePool().get(TYPE::String), [](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setArgName("level");
        e.addChoice(strdup("info"));
        e.addChoice(strdup("warn"));
        e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REQUIRED);
      });

  auto &recordType = this->createRecordType("type1", std::move(builder));

  //
  auto out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  auto args = createArgs("--time=1000", "info");
  auto ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_TRUE(ret);
  ASSERT_EQ(2, ret.index);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(1000, (*out)[1].asInt());
  ASSERT_EQ("info", (*out)[2].asStrRef().toString());

  // validation error (num format)
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  args = createArgs("-t", "qq", "AAA");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(1, ret.index);
  auto error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  const char *err = R"(cmd1: invalid argument: `qq' for -t option, must be decimal integer
Usage: cmd1 [OPTIONS] level

Options:
  -t, --time msec
  -h, --help       show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // validation error (int range)
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  args = createArgs("-t", "0", "--time=1001", "AAA");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(2, ret.index);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(0, (*out)[1].asInt());
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  err = R"(cmd1: invalid argument: `1001' for --time option, must be [0, 1000]
Usage: cmd1 [OPTIONS] level

Options:
  -t, --time msec
  -h, --help       show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // validation error (choice)
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  args = createArgs("-t", "0", "--time=1000", "Info");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(3, ret.index);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(1000, (*out)[1].asInt());
  error = state->getCallStack().takeThrownObject();

  err = R"(cmd1: invalid argument: `Info', must be {info, warn}
Usage: cmd1 [OPTIONS] level

Options:
  -t, --time msec
  -h, --help       show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // missing required options
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd2");
  args = createArgs("Info");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(0, ret.index);
  ASSERT_EQ("cmd2", (*out)[0].asStrRef().toString());
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  err = R"(cmd2: require -t or --time option
Usage: cmd2 [OPTIONS] level

Options:
  -t, --time msec
  -h, --help       show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // missing require arguments
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd1");
  args = createArgs("-t", "009");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(2, ret.index);
  ASSERT_EQ("cmd1", (*out)[0].asStrRef().toString());
  ASSERT_EQ(9, (*out)[1].asInt());
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());

  err = R"(cmd1: require `level' argument
Usage: cmd1 [OPTIONS] level

Options:
  -t, --time msec
  -h, --help       show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());
}

TEST_F(ArgParserTest, help) {
  ArgEntriesBuilder builder;
  builder.addHelp().add(this->typePool().get(TYPE::Bool), [](ArgEntry &e) {
    e.setParseOp(OptParseOp::NO_ARG);
    e.setArgName("output");
    e.setAttr(ArgEntryAttr::POSITIONAL);
  });

  auto &recordType = this->createRecordType(
      "type1", std::move(builder), CLIRecordType::Attr::VERBOSE, "this is a sample command line");

  //
  auto out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd11");
  auto args = createArgs("-h", "AAA");
  auto ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(1, ret.index);
  auto error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(0, error->getStatus());

  const char *err = R"(Usage: cmd11 [output]

this is a sample command line

Options:
  -h, --help  show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // help with invalid options
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd11");
  args = createArgs("-h", "--A");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(1, ret.index);
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(2, error->getStatus());

  err = R"(cmd11: invalid option: --A
Usage: cmd11 [output]

this is a sample command line

Options:
  -h, --help  show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());
}

TEST_F(ArgParserTest, shortUsage1) {
  ArgEntriesBuilder builder;
  builder.addHelp().add(this->typePool().get(TYPE::Bool), [](ArgEntry &e) {
    e.setParseOp(OptParseOp::NO_ARG);
    e.setArgName("output");
    e.setAttr(ArgEntryAttr::POSITIONAL);
  });

  auto &recordType = this->createRecordType("type1", std::move(builder), CLIRecordType::Attr{});

  // help (always show verbose usage without verbose attr)
  auto out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd11");
  auto args = createArgs("-h", "AAA");
  auto ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(1, ret.index);
  auto error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(0, error->getStatus());
  const char *err = R"(Usage: cmd11 [output]

Options:
  -h, --help  show this help message)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // invalid option
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd11");
  args = createArgs("-A");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(0, ret.index);
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(2, error->getStatus());
  err = R"(cmd11: invalid option: -A
See `cmd11 --help' for more information.)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // invalid option
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd11");
  args = createArgs("-ABC");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(0, ret.index);
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(2, error->getStatus());
  err = R"(cmd11: invalid option: -A
See `cmd11 --help' for more information.)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());
}

TEST_F(ArgParserTest, shortUsage2) {
  ArgEntriesBuilder builder;
  builder
      .add(this->typePool().get(TYPE::Int),
           [](ArgEntry &e) {
             e.setParseOp(OptParseOp::HAS_ARG);
             e.setArgName("time");
             e.setShortName('t');
             e.setIntRange(0, 1000);
             e.setAttr(ArgEntryAttr::REQUIRED);
           })
      .addHelp()
      .add(this->typePool().get(TYPE::Bool), [](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setArgName("output");
        e.setAttr(ArgEntryAttr::POSITIONAL | ArgEntryAttr::REQUIRED);
      });

  auto &recordType = this->createRecordType("type1", std::move(builder), CLIRecordType::Attr{});

  // missing option
  auto out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd11");
  auto args = createArgs("AAA");
  auto ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(0, ret.index);
  auto error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());
  const char *err = R"(cmd11: require -t option
See `cmd11 --help' for more information.)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // missing positional
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd12");
  args = createArgs("-t", "12");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(2, ret.index);
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());
  err = R"(cmd12: require `output' argument
See `cmd12 --help' for more information.)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());

  // option arg format
  out = createObject<BaseObject>(recordType);
  fillWithInvalid(*out);
  (*out)[0] = Value::createStr("cmd22");
  args = createArgs("-t", "121111111");
  ret = parseCommandLine(*this->state, *args, *out);
  ASSERT_FALSE(ret);
  ASSERT_EQ(1, ret.index);
  error = state->getCallStack().takeThrownObject();
  ASSERT_EQ(1, error->getStatus());
  err = R"(cmd22: invalid argument: `121111111' for -t option, must be [0, 1000]
See `cmd22 --help' for more information.)";
  ASSERT_EQ(err, error->getMessage().asStrRef().toString());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}