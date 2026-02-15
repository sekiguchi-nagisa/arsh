#include <cstdint>

#include <regex/dump.h>
#include <regex/parser.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  using namespace arsh;

  constexpr regex::Mode modes[] = {
      regex::Mode::BMP,
      regex::Mode::UNICODE,
      regex::Mode::UNICODE_SET,
  };
  const StringRef pattern(reinterpret_cast<const char *>(data), size);
  regex::Parser parser;
  for (auto mode : modes) {
    auto tree = parser(pattern, regex::Flag(mode, regex::Modifier::NONE));
    if (!parser.hasError()) {
      regex::TreeDumper dumper;
      auto buf = dumper(tree);
      assert(buf.size());
    }
  }
  return 0;
}