
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arsh/arsh.h>
#include <misc/format.hpp>

enum class FuzzPolicy {
  EVAL,
  COMPLETE, // default
};

static FuzzPolicy getFuzzPolicy() {
  struct {
    const char *value;
    FuzzPolicy policy;
  } table[] = {
      {"eval", FuzzPolicy::EVAL},         {"evaluation", FuzzPolicy::EVAL},
      {"complete", FuzzPolicy::COMPLETE}, {"completion", FuzzPolicy::COMPLETE},
      {"comp", FuzzPolicy::COMPLETE},
  };

  if (const char *env = getenv("ARSH_FUZZ_POLICY"); env && *env) {
    for (auto &e : table) {
      if (strcasecmp(env, e.value) == 0) {
        return e.policy;
      }
    }
  }
  return FuzzPolicy::COMPLETE;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static auto policy = getFuzzPolicy();

  switch (policy) {
  case FuzzPolicy::EVAL: {
    auto *state = ARState_createWithMode(AR_EXEC_MODE_COMPILE_ONLY);
    ARError dsError;
    ARState_eval(state, "<dummy>", reinterpret_cast<const char *>(data), size, &dsError);
    ARError_release(&dsError);
    ARState_delete(&state);
    break;
  }
  case FuzzPolicy::COMPLETE: {
    using namespace arsh;
    auto *state = ARState_create(); // kick completion
    std::string buf;
    StringRef ref(reinterpret_cast<const char *>(data), size);
    splitByDelim(ref, '\0', [&buf](StringRef sub, bool) {
      buf += sub;
      return true;
    });

    const char *argv[] = {
        "complete", "-q", "-d", "-s", "--", buf.c_str(), nullptr,
    };
    ARState_exec(state, const_cast<char **>(argv));
    ARState_delete(&state);
    break;
  }
  }
  return 0;
}