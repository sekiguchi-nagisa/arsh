#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>

#include <misc/unicode.hpp>
#include <regex/dump.h>
#include <regex/emit.h>
#include <regex/parser.h>

#include "../tools/json/serialize.h"

using namespace arsh;

#define JSONIFY(m) t(#m, m)

struct Target {
  std::string pattern;
  std::string input;
  regex::Mode mode{regex::Mode::BMP};
  regex::Modifier modifiers{};

  template <typename T>
  void jsonify(T &t) {
    std::vector<unsigned char> patternBytes;
    for (auto ch : this->pattern) {
      patternBytes.push_back(static_cast<unsigned char>(ch));
    }
    JSONIFY(patternBytes);
    JSONIFY(input);
    JSONIFY(mode);
    JSONIFY(modifiers);
  }
};

static void dump(FILE *fp, const Target &target) {
  auto tmp(target);
  json::JSONSerializer serializer;
  serializer(std::move(tmp));
  auto &json = serializer.get();
  auto out = json.serialize();
  fprintf(fp, "%s\n", out.c_str());
  fflush(fp);
}

static std::string toValidWTF8(const StringRef ref) {
  std::string ret;
  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    int codePoint = 0;
    if (unsigned int len = UnicodeUtil::wtf8ToCodePoint(iter, end, codePoint)) {
      ret.append(iter, len);
      iter += len;
    } else {
      iter++;
      ret += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
    }
  }
  return ret;
}

static Target createTarget(const uint8_t *data, const size_t size) {
  constexpr uint8_t MODIFIER_MASK = toUnderlying(regex::Modifier::DOT_ALL) |
                                    toUnderlying(regex::Modifier::IGNORE_CASE) |
                                    toUnderlying(regex::Modifier::MULTILINE);

  FuzzedDataProvider fdp(data, size);
  Target target;
  target.mode = fdp.PickValueInArray<regex::Mode>({
      regex::Mode::BMP,
      regex::Mode::UNICODE,
      regex::Mode::UNICODE_SET,
  });
  const uint8_t modBits = fdp.ConsumeIntegral<uint8_t>() & MODIFIER_MASK;
  target.modifiers = static_cast<regex::Modifier>(modBits);

  const size_t remaining = fdp.remaining_bytes();
  const size_t patLen = fdp.ConsumeIntegralInRange<size_t>(0, remaining);
  target.pattern = fdp.ConsumeBytesAsString(patLen);
  target.input = toValidWTF8(fdp.ConsumeRemainingBytesAsString());

  return target;
}

static std::string formatCaptures(const std::vector<regex::Capture> &captures) {
  std::string ret;
  for (auto &c : captures) {
    if (!c) {
      ret += "(unset)\n";
      continue;
    }
    ret += "(offset=";
    ret += std::to_string(c.offset);
    ret += ", size=";
    ret += std::to_string(c.size);
    ret += ")\n";
  }
  return ret;
}

static void match(const regex::Regex &re, const StringRef input, const bool print) {
  std::vector<regex::Capture> captures;
  regex::Timer timer(std::chrono::milliseconds(300));
  auto status = regex::match(re, input, captures, makeObserver(timer));
  if (print) {
    fprintf(stderr, "input: `%s'\n", input.toString().c_str());
  }
  if (status == regex::MatchStatus::OK) {
    auto str = formatCaptures(captures);
    if (print) {
      fwrite(str.c_str(), sizeof(char), str.size(), stderr);
    }
  }
  if (print) {
    fprintf(stderr, "%s\n", toString(status));
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const static bool print = getenv("ARSH_SUPPRESS_COMPILE_ERROR") == nullptr;
  const auto target = createTarget(data, size);
  if (getenv("ARSH_FUZZ_DUMP_TARGET")) {
    dump(stdout, target);
  }
  regex::Parser parser;
  auto tree = parser(target.pattern, regex::Flag(target.mode, target.modifiers));
  if (parser.hasError()) {
    if (print) {
      auto token = parser.getError()->token;
      fprintf(stderr, "[error] %s\n at %s\n", parser.getError()->message.c_str(),
              token.str().c_str());
    }
    return 0;
  }
  regex::TreeDumper dumper;
  auto buf = dumper(tree);
  assert(buf.size());
  if (print) {
    fprintf(stderr, "%s\n", buf.c_str());
  }
  regex::CodeGen codeGen;
  if (auto re = codeGen(std::move(tree)); re.hasValue()) {
    regex::RegexDumper reDumper;
    buf = reDumper(re.unwrap());
    assert(buf.size());
    if (print) {
      fprintf(stderr, "%s\n", buf.c_str());
    }
    match(re.unwrap(), target.input, print);
  }
  return 0;
}