#include "gtest/gtest.h"

#include <arg_parser.h>
#include <vm.h>
#include <ydsh/ydsh.h>

using namespace ydsh;

class ArgEntriesBuilder {
private:
  std::vector<ArgEntry> values;
  unsigned int offset{0};

public:
  template <typename Func>
  static constexpr bool func_requirement_v =
      std::is_same_v<void, std::invoke_result_t<Func, ArgEntry &>>;

  template <typename Func, enable_when<func_requirement_v<Func>> = nullptr>
  ArgEntriesBuilder &add(Func func) {
    this->values.emplace_back(this->offset++);
    func(this->values.back());
    return *this;
  }

  std::vector<ArgEntry> build() && { return std::move(this->values); }
};

template <typename... T>
static ObjPtr<ArrayObject> createArgs(T &&...args) {
  std::vector<DSValue> values = {DSValue::createStr(args)...};
  auto v = DSValue::create<ArrayObject>(toUnderlying(TYPE::StringArray), std::move(values));
  return toObjPtr<ArrayObject>(v);
}

static std::string toStringAt(const ArrayObject &obj, size_t index) {
  return obj.getValues()[index].asStrRef().toString();
}

class ArgParserTest : public ::testing::Test {
protected:
  DSState *state{nullptr};

public:
  void SetUp() override { this->state = DSState_create(); }

  void TearDown() override { DSState_delete(&this->state); }

  TypePool &typePool() { return this->state->typePool; }

  const ArgsRecordType &createRecordType(const char *typeName, ArgEntriesBuilder &&builder) {
    const auto modId = ModId{1};
    auto ret = this->typePool().createArgsRecordType(typeName, modId);
    assert(ret);
    (void)ret;
    auto entries = std::move(builder).build();
    std::unordered_map<std::string, HandlePtr> handles;
    for (size_t i = 0; i < entries.size(); i++) {
      std::string name = "field_";
      name += std::to_string(i);
      auto handle = HandlePtr ::create(this->typePool().get(TYPE::String), i, HandleKind::VAR,
                                       HandleAttr::UNCAPTURED, modId);
      handles.emplace(std::move(name), std::move(handle));
    }
    auto &type = *cast<ArgsRecordType>(ret.asOk());
    ret = this->typePool().finalizeArgsRecordType(type, std::move(handles), std::move(entries));
    assert(ret);
    return type;
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
      })
      .add([](ArgEntry &e) {
        e.setParseOp(OptParseOp::NO_ARG);
        e.setShortName('d');
        e.setDetail("enable debug");
        e.setAttr(ArgEntryAttr::STORE_FALSE);
      });

  auto &recordType = this->createRecordType("type1", std::move(builder));
  auto &entries = recordType.getEntries();
  ASSERT_EQ(3, entries.size());
  ASSERT_EQ(OptParseOp::NO_ARG, entries[0].getParseOp());
  ASSERT_EQ('s', entries[0].getShortName());
  ASSERT_STREQ("status", entries[0].getLongName());
  ASSERT_EQ(OptParseOp::HAS_ARG, entries[1].getParseOp());
  ASSERT_EQ('o', entries[1].getShortName());
  ASSERT_STREQ("output", entries[1].getLongName());
  ASSERT_EQ(OptParseOp::NO_ARG, entries[2].getParseOp());
  ASSERT_EQ('d', entries[2].getShortName());
  ASSERT_FALSE(entries[2].getLongName());

  auto args = createArgs("-s", "--output", "AAA", "-d", "BBB", "CCC");
  ASSERT_EQ(6, args->size());
  ASSERT_EQ("-s", toStringAt(*args, 0));
  ASSERT_EQ("--output", toStringAt(*args, 1));
  ASSERT_EQ("AAA", toStringAt(*args, 2));
  ASSERT_EQ("-d", toStringAt(*args, 3));
  ASSERT_EQ("BBB", toStringAt(*args, 4));
  ASSERT_EQ("CCC", toStringAt(*args, 5));

  auto ret =
      this->typePool().createReifiedType(this->typePool().getArgParserTemplate(), {&recordType});
  ASSERT_TRUE(ret);
  auto &parserType = cast<ArgParserType>(*ret.asOk());
  auto parser = toObjPtr<ArgParserObject>(
      DSValue::create<ArgParserObject>(parserType, DSValue::createStr("cmd1")));
  auto out = toObjPtr<BaseObject>(DSValue::create<BaseObject>(recordType));
  ASSERT_FALSE((*out)[0]);
  ASSERT_FALSE((*out)[1]);
  ASSERT_FALSE((*out)[2]);
  bool s = parser->parseAll(*this->state, *args, *out);
  ASSERT_TRUE(s);
  ASSERT_TRUE((*out)[0].asBool());
  ASSERT_EQ("AAA", (*out)[1].asStrRef().toString());
  ASSERT_FALSE((*out)[2].asBool());

  const char *help = R"(Usage: cmd1 [OPTIONS]

Options:
  -s, --status
  -o, --output arg
  -d                enable debug
  -h, --help        show this help message)";
  std::string v;
  parser->formatUsage(true, v);
  ASSERT_EQ(help, v);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}