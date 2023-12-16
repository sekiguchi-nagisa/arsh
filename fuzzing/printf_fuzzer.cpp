
#include <ydsh/arsh.h>

#include "../src/misc/buffer.hpp"
#include "../src/misc/string_ref.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  using namespace ydsh;

  // prepare
  FlexBuffer<char *> argv;
  argv.push_back(strdup("printf"));
  argv.push_back(strdup("--"));

  StringRef ref(reinterpret_cast<const char *>(data), size);
  for (StringRef::size_type pos = 0; pos < ref.size();) {
    auto r = ref.find('\0', pos);
    auto sub = ref.slice(pos, r);

    auto *ptr = static_cast<char *>(malloc(sub.size() + 1));
    memcpy(ptr, sub.data(), sub.size());
    ptr[sub.size()] = '\0';
    argv.push_back(ptr);

    if (r != StringRef::npos) {
      pos = r + 1;
    } else {
      break;
    }
  }
  argv.push_back(nullptr);

  setenv("YDSH_PRINTF_FUZZ", "on", 1);
  auto *state = DSState_create();

  // just print
  DSState_exec(state, argv.data());

  // -v var
  argv.insert(argv.begin() + 1, strdup("-v"));
  argv.insert(argv.begin() + 2, strdup("var"));
  assert(StringRef(argv[0]) == "printf");
  assert(StringRef(argv[1]) == "-v");
  assert(StringRef(argv[2]) == "var");
  assert(StringRef(argv[3]) == "--");
  DSState_exec(state, argv.data());

  // release resource
  DSState_delete(&state);
  for (auto &e : argv) {
    free(e);
  }
  return 0;
}