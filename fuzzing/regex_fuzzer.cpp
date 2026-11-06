#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>

#include <misc/unicode.hpp>
#include <regex/dump.h>
#include <regex/emit.h>
#include <regex/match_context.h>
#include <regex/parser.h>

#include "../test/regex/dump.hpp"
#include "../tools/json/serialize.h"

using namespace arsh;

static void dump(FILE *fp, const regex::Target &target) {
  auto tmp(target);
  tmp.beforeSerialize();
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

static regex::Target createTarget(const uint8_t *data, const size_t size) {
  constexpr uint8_t MODIFIER_MASK = toUnderlying(regex::Modifier::DOT_ALL) |
                                    toUnderlying(regex::Modifier::IGNORE_CASE) |
                                    toUnderlying(regex::Modifier::MULTILINE);

  FuzzedDataProvider fdp(data, size);
  regex::Target target;
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

static regex::MatchStatus matchBoth(const regex::Regex &re, const StringRef text,
                                    std::string &out) {
  regex::Timer timer(std::chrono::milliseconds(300));

  // on
  std::string onMatches;
  const auto onStatus = regex::matchWithPrefilter(re, text, true, makeObserver(timer), onMatches);
  if (onStatus == regex::MatchStatus::TIMEOUT) {
    return onStatus;
  }

  // off
  std::string offMatches;
  const auto offStatus =
      regex::matchWithPrefilter(re, text, false, makeObserver(timer), offMatches);
  if (offStatus == regex::MatchStatus::TIMEOUT) {
    return offStatus;
  }

  // compare results
  if (onStatus != offStatus || onMatches != offMatches) {
    regex::dumpDiffFailure(stderr, onStatus, onMatches, offStatus, offMatches);
    fatal("prefilter-diff\n");
  }
  out = onMatches;
  return onStatus;
}

static void match(const regex::Regex &re, const StringRef input, const bool print) {
  std::string out;
  const auto status = matchBoth(re, input, out);
  if (print) {
    fprintf(stderr, "input: `%s'\n", input.toString().c_str());
  }
  if (status == regex::MatchStatus::OK) {
    if (print) {
      fwrite(out.c_str(), sizeof(char), out.size(), stderr);
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
  auto tree = parser(get<std::string>(target.pattern), regex::Flag(target.mode, target.modifiers));
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
    match(re.unwrap(), get<std::string>(target.input), print);
  }
  return 0;
}