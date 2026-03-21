#include <cstdint>

#include <misc/unicode.hpp>
#include <regex/dump.h>
#include <regex/emit.h>
#include <regex/parser.h>

using namespace arsh;

struct Target {
  std::string pattern;
  std::string input;
};

static std::string toValidUtf8(const StringRef ref) {
  std::string ret;
  const char *end = ref.end();
  for (const char *iter = ref.begin(); iter != end;) {
    int codePoint = 0;
    if (unsigned int len = UnicodeUtil::utf8ToCodePoint(iter, end, codePoint);
        len && UnicodeUtil::isValidCodePoint(codePoint)) {
      ret.append(iter, len);
      iter += len;
    } else {
      iter++;
      ret += UnicodeUtil::REPLACEMENT_CHAR_UTF8;
    }
  }
  return ret;
}

static Target create(const uint8_t *data, const size_t size) {
  StringRef value(reinterpret_cast<const char *>(data), size);
  auto pattern = value.substr(0, size / 2);
  auto input = toValidUtf8(value.slice(size / 2, value.size()));
  return {
      .pattern = pattern.toString(),
      .input = std::move(input),
  };
}

static std::string formatCaptures(const FlexBuffer<regex::Capture> &captures) {
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
  FlexBuffer<regex::Capture> captures;
  regex::Timer timer(std::chrono::seconds(2));
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
  constexpr regex::Mode modes[] = {
      regex::Mode::BMP,
      regex::Mode::UNICODE,
      regex::Mode::UNICODE_SET,
  };
  const bool print = getenv("ARSH_SUPPRESS_COMPILE_ERROR") == nullptr;
  auto [pattern, input] = create(data, size);
  regex::Parser parser;
  for (auto mode : modes) {
    constexpr regex::Modifier modifiers[] = {
        regex::Modifier::NONE,
        regex::Modifier::DOT_ALL | regex::Modifier::IGNORE_CASE | regex::Modifier::MULTILINE,
    };
    for (auto modifier : modifiers) {
      auto tree = parser(pattern, regex::Flag(mode, modifier));
      if (parser.hasError()) {
        if (print) {
          auto token = parser.getError()->token;
          fprintf(stderr, "[error] %s\n at %s\n", parser.getError()->message.c_str(),
                  token.str().c_str());
        }
        continue;
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
        match(re.unwrap(), input, print);
      }
    }
  }
  return 0;
}