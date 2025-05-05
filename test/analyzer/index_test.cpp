
#include <config.h>
#include <format_signature.h>

#include "index_test.hpp"

TEST_F(IndexTest, hover) {
  // variable or function
  ASSERT_NO_FATAL_FAILURE(this->hover("let A = 34\n$A", 1, "```arsh\nlet A: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("import-env HOME\n$HOME", 1, "```arsh\nimportenv HOME: String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("export-env ZZZ = 'hoge'\n$ZZZ", 1, "```arsh\nexportenv ZZZ: String\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("function hoge($s : Int) {}\n$hoge", 1,
                                      "```arsh\nfunction hoge(s: Int): Void\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("function hoge($ss : Int) {\n$ss; }", 1, "```arsh\nvar ss: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("function hoge($_sss : Int) {}\n$hoge(\n$_sss: 34)", 2,
                                      "```arsh\nvar _sss: Int\n```"));

  // user-defined command
  ASSERT_NO_FATAL_FAILURE(this->hover("hoge(){}\nhoge", 1, "```arsh\nhoge(): Bool\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("usage() : Nothing { throw 34; }\nusage", 1, "```arsh\nusage(): Nothing\n```"));

  // user-defined error type
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef App : OutOfRangeError\n34 is\nApp", 2,
                                      "```arsh\ntype App: OutOfRangeError\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("type AppError : Error; type API : AppError\n34 is\nAPI", 2,
                                      "```arsh\ntype API: AppError\n```"));

  // user-defined type with explicit constructor
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef Interval() { var begin = 34; }; var a = new Interval();\n$a",
                  Position{.line = 1, .character = 0}, "```arsh\nvar a: Interval\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef Interval(s : Int) { var n = $s; let value = new "
                                      "Interval?(); }\nvar a = new Interval();",
                                      Position{.line = 1, .character = 15}, R"(```arsh
type Interval(s: Int) {
    var n: Int
    let value: Interval?
}
```)"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { var begin = 34; }\nnew AAA().\nbegin",
                                      Position{.line = 2, .character = 1},
                                      "```arsh\nvar begin: Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { var begin = 34; \n$begin;}",
                                      Position{.line = 1, .character = 3},
                                      "```arsh\nvar begin: Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { typedef Type = Int; }\n23 is AAA.\nType",
                                      Position{.line = 2, .character = 3},
                                      "```arsh\ntype Type = Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA() { typedef Type = Int; 34 is \nType; }",
                                      Position{.line = 1, .character = 3},
                                      "```arsh\ntype Type = Int for AAA\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef AAA($_sss : Int) {\n$_sss; }", 1, "```arsh\nvar _sss: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AAA($_sss : Int) {};\nnew AAA(\n$_sss: 111)", 2,
                                      "```arsh\nvar _sss: Int\n```"));

  // user-defined type with implicit constructor
  ASSERT_NO_FATAL_FAILURE(this->hover("type Interval { var n : Int; let next : Interval?; "
                                      "}\nvar aaaa = new Interval(2, $none);",
                                      Position{.line = 1, .character = 20}, R"(```arsh
type Interval(n: Int, next: Interval?) {
    var n: Int
    let next: Interval?
}
```)"));

  ASSERT_NO_FATAL_FAILURE(this->hover(
      "typedef Interval { var value : Interval?; }; var a = new Interval($none);\n$a.value",
      Position{.line = 1, .character = 3}, "```arsh\nvar value: Interval? for Interval\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover(
      "typedef Interval() { var value = new Interval?(); }; var aaa = new [[Interval]]();\n$aaa",
      Position{.line = 1, .character = 2}, "```arsh\nvar aaa: [[Interval]]\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("type Interval { var n:Int; let next: Interval?; }\n"
                                      "new Interval(\n$next: $None, $n: 1234)",
                                      Position{.line = 2, .character = 2},
                                      "```arsh\nlet next: Interval? for Interval\n```"));

  // user-defined method
  ASSERT_NO_FATAL_FAILURE(this->hover("typedef INT(a : Int) { var v = $a; }\n"
                                      "function value():Int for INT { return $this.v; }\n"
                                      "new INT(12).value()",
                                      Position{.line = 2, .character = 13},
                                      "```arsh\nfunction value(): Int for INT\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("function value():Int for String { \nreturn $this.size(); }",
                                      Position{.line = 1, .character = 8},
                                      "```arsh\nlet this: String\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("typedef AppError : Error; \n"
                                      "function size(aaa: Int): Int for AppError { return\n$aaa; }",
                                      2, "```arsh\nvar aaa: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("typedef AppError : Error; \n"
                  "function size(aaa: Int?, $bbb: Int?): Int for AppError { return $aaa!; }\n"
                  "new AppError('').size(\n$bbb:333)",
                  3, "```arsh\nvar bbb: Int?\n```"));

  // here doc
  ASSERT_NO_FATAL_FAILURE(this->hover(R"(cat << EOF
this is a pennn""
EOF)",
                                      Position{.line = 0, .character = 9},
                                      "```md\nhere document start word\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover(R"(cat << EOF
this is a pennn""
EOF)",
                                      Position{.line = 2, .character = 1},
                                      "```md\nhere document start word\n```"));
}

TEST_F(IndexTest, hoverBuiltin) {
  // builtin type
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nInt", 1, ""));
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nError", 1, ""));
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nShellExit", 1, ""));
  ASSERT_NO_FATAL_FAILURE(this->hover("34 is\nAssertionFailed", 1, ""));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("34 is\nArithmeticError", 1, "```arsh\ntype ArithmeticError: Error\n```"));

  // builtin variable or type alias
  ASSERT_NO_FATAL_FAILURE(this->hover("$?", 0, "```arsh\nvar ?: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("hoge() { \n$@;}", 1, "```arsh\nlet @: [String]\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$VERSION", 0,
                                      "```arsh\nconst VERSION = '" X_INFO_VERSION_CORE "'"
                                      "\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$true is\nBoolean", 1, "```arsh\ntype Boolean = Bool\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("$SCRIPT_NAME", 0, "```arsh\nconst SCRIPT_NAME = '/dummy_10'\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$SCRIPT_DIR", 0, "```arsh\nconst SCRIPT_DIR = '/'\n```"));

  // builtin command
  ASSERT_NO_FATAL_FAILURE(
      this->hover(":", 0,
                  "```md\n"
                  ":: : \n"
                  "    Null command.  Always success (exit status is 0).\n```"));

  // builtin tuple or method
  ASSERT_NO_FATAL_FAILURE(this->hover("var a = (34, $false, '');$a._2\n$a._2",
                                      Position{.line = 1, .character = 3},
                                      "```arsh\nvar _2: String for (Int, Bool, String)\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("''.size();[1].size()\n[0].size()",
                                      Position{.line = 1, .character = 5},
                                      "```arsh\nfunction size(): Int for [Int]\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("''.slice(0)", Position{.line = 0, .character = 5},
                  "```arsh\nfunction slice(start: Int, end: Int?): String for String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("[23:(12,(34,56,))].\nclear()", Position{.line = 1, .character = 2},
                  "```arsh\nfunction clear(): Void for [Int : (Int, (Int, Int))]\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("''.equals('')", Position{.line = 0, .character = 5},
                  "```arsh\nfunction equals(target: String): Bool for String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("''.compare('')", Position{.line = 0, .character = 5},
                  "```arsh\nfunction compare(target: String): Int for String\n```"));

  // builtin named param
  ASSERT_NO_FATAL_FAILURE(
      this->hover("'2345'.slice(\n$end:2345)", 1, "```arsh\nvar end: Int?\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("[1234].slice(\n$start:2345)", 1, "```arsh\nvar start: Int\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("[1234].slice(\n$end:345,$start:2345)", 1, "```arsh\nvar end: Int?\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("['23':1234].remove(\n$key:'2')", 1, "```arsh\nvar key: String\n```"));
  ASSERT_NO_FATAL_FAILURE(
      this->hover("''.compare(\n$target:'2345')", 1, "```arsh\nvar target: String\n```"));
}

TEST_F(IndexTest, hoverMod) {
  // source
  TempFileFactory tempFileFactory("arsh_index");
  auto fileName = tempFileFactory.createTempFile(X_INFO_VERSION_CORE "_.ds",
                                                 R"(
var AAA = 'hello'
)");
  std::string src = "source ";
  src += fileName;
  src += " as mod\n$mod";
  ASSERT_NO_FATAL_FAILURE(
      this->hover(src.c_str(), 1, format("```arsh\nsource %s as mod\n```", fileName.c_str())));

  src = "source ";
  src += tempFileFactory.getTempDirName();
  src += "/";
  int chars = static_cast<int>(src.size()) + 5;
  src += "${VERSION}_.ds";
  ASSERT_NO_FATAL_FAILURE(this->hover(src.c_str(), Position{.line = 0, .character = chars},
                                      "```arsh\nconst VERSION = '" X_INFO_VERSION_CORE "'\n```"));
}

TEST_F(IndexTest, hoverConst) {
  ASSERT_NO_FATAL_FAILURE(this->hover("$TRUE", 0, "```arsh\nconst TRUE = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$True", 0, "```arsh\nconst True = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$true", 0, "```arsh\nconst true = true\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$FALSE", 0, "```arsh\nconst FALSE = false\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$False", 0, "```arsh\nconst False = false\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$false", 0, "```arsh\nconst false = false\n```"));

  ASSERT_NO_FATAL_FAILURE(this->hover("$SIGHUP", 0, "```arsh\nconst SIGHUP = signal(1)\n```"));
  ASSERT_NO_FATAL_FAILURE(this->hover("$SIGKILL", 0, "```arsh\nconst SIGKILL = signal(9)\n```"));
}

TEST_F(IndexTest, hoverUsage1) {
  const char *src = R"([<CLI>]
typedef AAA() {
  [<Flag(short: "s", long: "status", help: "dump internal status")>]
  var s = $false

  [<Option(help: "specify output target", opt: $true)>]
  var output : String?

  [<Arg(required: $true)>]
  var files : [String]

  [<Arg()>]  # error
  var dest : String?
}
new AAA()
)";

  const char *out = R"(```arsh
type AAA() {
    var %name: String
    var s: Bool
    var output: String?
    var files: [String]
    var dest: String?
}
```

**command line**
```md
Usage:  [OPTIONS] FILES...

Options:
  -s, --status       dump internal status
  --output[=OUTPUT]  specify output target
  -h, --help         show this help message
```)";
  ASSERT_NO_FATAL_FAILURE(this->hover(src, {14, 6}, out));
}

TEST_F(IndexTest, hoverUsage2) {
  const char *src = R"([<CLI()>]
typedef Param() {
  [<Flag(short: "s", long: "status", help: "dump internal status")>]
  var s = $false

  [<Option(help: "specify output target", opt: $true, short: 'o', long: 'output')>]
  var output : String?

  [<Arg(required: $true)>]
  var files : [String]

  [<Arg()>]  # error
  var dest : String?
}

fff($p : Param) { echo $p; }
fff
)";

  const char *out = R"(```arsh
fff(): Bool
```

**command line**
```md
Usage: fff [OPTIONS] FILES...

Options:
  -s, --status                   dump internal status
  -o[OUTPUT], --output[=OUTPUT]  specify output target
  -h, --help                     show this help message
```)";
  ASSERT_NO_FATAL_FAILURE(this->hover(src, {16, 2}, out));
}

TEST_F(IndexTest, hoverUsage3) {
  const char *src = R"([<CLI(toplevel: $true)>]
type Param() {}
fff($p : Param) { echo $p; }
fff
)";

  const char *out = R"(```arsh
fff(): Bool
```

**command line**
```md
Usage: <$ARG0>

Options:
  -h, --help  show this help message
```)";
  ASSERT_NO_FATAL_FAILURE(this->hover(src, {3, 2}, out));
}

TEST_F(IndexTest, docSymbol1) {
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::VAR));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::LET));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::IMPORT_ENV));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::EXPORT_ENV));
  ASSERT_EQ(SymbolKind::Variable, toSymbolKind(DeclSymbol::Kind::MOD));
  ASSERT_EQ(SymbolKind::Field, toSymbolKind(DeclSymbol::Kind::VAR, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Field, toSymbolKind(DeclSymbol::Kind::LET, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Field,
            toSymbolKind(DeclSymbol::Kind::IMPORT_ENV, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Field,
            toSymbolKind(DeclSymbol::Kind::EXPORT_ENV, DeclSymbol::Attr::MEMBER));
  ASSERT_EQ(SymbolKind::Constant, toSymbolKind(DeclSymbol::Kind::CONST));
  ASSERT_EQ(SymbolKind::Constant, toSymbolKind(DeclSymbol::Kind::MOD_CONST));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::FUNC));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::CMD));
  ASSERT_EQ(SymbolKind::Function, toSymbolKind(DeclSymbol::Kind::BUILTIN_CMD));
  ASSERT_EQ(SymbolKind::Method, toSymbolKind(DeclSymbol::Kind::METHOD));
  ASSERT_EQ(SymbolKind::Constructor, toSymbolKind(DeclSymbol::Kind::CONSTRUCTOR));
  ASSERT_EQ(SymbolKind::Class, toSymbolKind(DeclSymbol::Kind::TYPE_ALIAS));
  ASSERT_EQ(SymbolKind::Class, toSymbolKind(DeclSymbol::Kind::ERROR_TYPE_DEF));
  ASSERT_EQ(SymbolKind::String, toSymbolKind(DeclSymbol::Kind::HERE_START)); // normally unused
}

TEST_F(IndexTest, docSymbol2) {
  unsigned short modId;
  const char *content = R"(
importenv ZZZ : $HOME
function ggg(aaa:Int) {
  var bbb = 2345; return $aaa + $bbb
}
type Interval {
  let begin: Int
  let end: Int
 }
function dist(): Int for Interval {
  return $this.end - $this.begin
}
error(): Nothing {
  var msg = $@.join('');
  throw new Error("$msg")
}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 11, .symbolSize = 27}));
  auto src = this->srcMan.findById(ModId{modId});
  ASSERT_TRUE(src);
  auto values = generateDocumentSymbols(this->indexes, *src);
  ASSERT_EQ(5, values.size());

  ASSERT_EQ("ZZZ", values[0].name);
  ASSERT_EQ(SymbolKind::Variable, values[0].kind);
  ASSERT_EQ("String", values[0].detail);
  ASSERT_EQ("(1:10~1:13)", values[0].selectionRange.toString());
  ASSERT_EQ("(1:0~1:21)", values[0].range.toString());

  ASSERT_EQ("ggg", values[1].name);
  ASSERT_EQ(SymbolKind::Function, values[1].kind);
  ASSERT_EQ("(aaa: Int): Void", values[1].detail);
  ASSERT_EQ("(2:9~2:12)", values[1].selectionRange.toString());
  ASSERT_EQ("(2:0~4:1)", values[1].range.toString());
  ASSERT_TRUE(values[1].children.hasValue());
  ASSERT_EQ(2, values[1].children.unwrap().size());
  ASSERT_EQ("aaa", values[1].children.unwrap()[0].name);
  ASSERT_EQ(SymbolKind::Variable, values[1].children.unwrap()[0].kind);
  ASSERT_EQ("Int", values[1].children.unwrap()[0].detail);
  ASSERT_EQ("bbb", values[1].children.unwrap()[1].name);
  ASSERT_EQ(SymbolKind::Variable, values[1].children.unwrap()[1].kind);
  ASSERT_EQ("Int", values[1].children.unwrap()[1].detail);

  ASSERT_EQ("Interval", values[2].name);
  ASSERT_EQ(SymbolKind::Constructor, values[2].kind);
  ASSERT_EQ("(begin: Int, end: Int)", values[2].detail);
  ASSERT_EQ("(5:5~5:13)", values[2].selectionRange.toString());
  ASSERT_EQ("(5:0~8:2)", values[2].range.toString());
  ASSERT_TRUE(values[2].children.hasValue());
  ASSERT_EQ(2, values[2].children.unwrap().size());
  ASSERT_EQ("begin", values[2].children.unwrap()[0].name);
  ASSERT_EQ(SymbolKind::Field, values[2].children.unwrap()[0].kind);
  ASSERT_EQ("Int for Interval", values[2].children.unwrap()[0].detail);
  ASSERT_EQ("end", values[2].children.unwrap()[1].name);
  ASSERT_EQ(SymbolKind::Field, values[2].children.unwrap()[1].kind);
  ASSERT_EQ("Int for Interval", values[2].children.unwrap()[1].detail);

  ASSERT_EQ("dist", values[3].name);
  ASSERT_EQ(SymbolKind::Method, values[3].kind);
  ASSERT_EQ("(): Int for Interval", values[3].detail);
  ASSERT_EQ("(9:9~9:13)", values[3].selectionRange.toString());
  ASSERT_EQ("(9:0~11:1)", values[3].range.toString());
  ASSERT_TRUE(values[3].children.hasValue());
  ASSERT_EQ(0, values[3].children.unwrap().size());

  ASSERT_EQ("error", values[4].name);
  ASSERT_EQ(SymbolKind::Function, values[4].kind);
  ASSERT_EQ("(): Nothing", values[4].detail);
  ASSERT_EQ("(12:0~12:5)", values[4].selectionRange.toString());
  ASSERT_EQ("(12:0~15:1)", values[4].range.toString());
  ASSERT_TRUE(values[4].children.hasValue());
  ASSERT_EQ(1, values[4].children.unwrap().size());
  ASSERT_EQ("msg", values[4].children.unwrap()[0].name);
  ASSERT_EQ(SymbolKind::Variable, values[4].children.unwrap()[0].kind);
  ASSERT_EQ("String", values[4].children.unwrap()[0].detail);
}

TEST_F(IndexTest, docSymbol3) {
  TempFileFactory tempFileFactory("arsh_index");
  auto fileName = tempFileFactory.createTempFile("mod.ds",
                                                 R"(
  var aaa = 2133421
)");

  unsigned short modId;
  auto content = format(R"(source %s \
as mod
type Str = String
type APIError: Error
)",
                        fileName.c_str());
  ASSERT_NO_FATAL_FAILURE(
      this->doAnalyze(content.c_str(), modId, {.declSize = 3, .symbolSize = 5}));
  ASSERT_EQ(1, modId);

  auto src = this->srcMan.findById(ModId{modId});
  ASSERT_TRUE(src);
  auto values = generateDocumentSymbols(this->indexes, *src);
  ASSERT_EQ(3, values.size());

  ASSERT_EQ("mod", values[0].name);
  ASSERT_EQ(SymbolKind::Variable, values[0].kind);
  ASSERT_EQ("%mod2", values[0].detail);
  ASSERT_EQ("(1:3~1:6)", values[0].selectionRange.toString());
  ASSERT_FALSE(values[0].children.hasValue());

  ASSERT_EQ("Str", values[1].name);
  ASSERT_EQ(SymbolKind::Class, values[1].kind);
  ASSERT_EQ("String", values[1].detail);
  ASSERT_EQ("(2:5~2:8)", values[1].selectionRange.toString());
  ASSERT_EQ("(2:0~2:17)", values[1].range.toString());
  ASSERT_FALSE(values[1].children.hasValue());

  ASSERT_EQ("APIError", values[2].name);
  ASSERT_EQ(SymbolKind::Class, values[2].kind);
  ASSERT_EQ("", values[2].detail);
  ASSERT_EQ("(3:5~3:13)", values[2].selectionRange.toString());
  ASSERT_EQ("(3:0~3:20)", values[2].range.toString());
  ASSERT_FALSE(values[2].children.hasValue());
}

static std::string resolvePackedParamType(const Type &type) {
  if (isa<ArrayType>(type)) {
    return cast<ArrayType>(type).getElementType().getNameRef().toString();
  } else if (isa<MapType>(type)) {
    auto &mapType = cast<MapType>(type);
    return mapType.getKeyType().getNameRef().toString() + ";" +
           mapType.getValueType().getNameRef().toString();
  } else {
    return "";
  }
}

TEST_F(IndexTest, signature) {
  TypePool pool;

  std::vector<TypePool::Key> keys;
  for (auto &e : pool.getMethodMap()) {
    keys.push_back(e.first);
  }

  for (auto &key : keys) {
    auto &type = pool.get(key.id);
    SCOPED_TRACE("key(" + key.ref.toString() + ", " + type.getName() + ")");
    auto iter = pool.getMethodMap().find(key);
    ASSERT_TRUE(iter != pool.getMethodMap().end());
    unsigned int index = iter->second ? iter->second.handle()->getIndex() : iter->second.index();
    auto handle = pool.allocNativeMethodHandle(type, index);
    ASSERT_TRUE(handle);
    std::string expect = "function ";
    expect += key.ref;
    formatMethodSignature(key.ref == OP_INIT ? nullptr : &type, *handle, expect, nullptr);

    std::string actual = "function ";
    actual += key.ref;
    formatNativeMethodSignature(index, resolvePackedParamType(type), actual);

    ASSERT_EQ(expect, actual);
  }
}

TEST_F(IndexTest, inheritance) {
  unsigned short modId;
  const char *content = R"(
typedef APIError: ArithmeticError
typedef APIError2 : APIError
typedef AAA() {}
[<CLI>]
typedef BBB() {}
)";
  ASSERT_NO_FATAL_FAILURE(this->doAnalyze(content, modId, {.declSize = 4, .symbolSize = 6}));

  const auto builtinIndex = this->indexes.find(BUILTIN_MOD_ID);
  ASSERT_TRUE(builtinIndex);
  const auto index = this->indexes.find(static_cast<ModId>(modId));
  ASSERT_TRUE(index);

  {
    auto *type = index->findBaseType(toQualifiedTypeName("APIError2", static_cast<ModId>(modId)));
    ASSERT_TRUE(type);
    ASSERT_EQ(toQualifiedTypeName("APIError", static_cast<ModId>(modId)), type->getValue());
    ASSERT_EQ(static_cast<ModId>(modId), type->resolveBelongedModId());

    type = index->findBaseType(toQualifiedTypeName("APIError", static_cast<ModId>(modId)));
    ASSERT_TRUE(type);
    ASSERT_EQ("ArithmeticError", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("ArithmeticError");
    ASSERT_TRUE(type);
    ASSERT_EQ("Error", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("Error");
    ASSERT_TRUE(type);
    ASSERT_EQ("Throwable", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("Throwable");
    ASSERT_FALSE(type);
  }

  {
    auto *type = index->findBaseType(toQualifiedTypeName("AAA", static_cast<ModId>(modId)));
    ASSERT_TRUE(type);
    ASSERT_EQ("%Ord_", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("%Ord_");
    ASSERT_FALSE(type);
  }

  {
    auto *type = index->findBaseType(toQualifiedTypeName("BBB", static_cast<ModId>(modId)));
    ASSERT_TRUE(type);
    ASSERT_EQ("CLI", type->getValue());
    ASSERT_EQ(BUILTIN_MOD_ID, type->resolveBelongedModId());

    type = builtinIndex->findBaseType("CLI");
    ASSERT_FALSE(type);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}