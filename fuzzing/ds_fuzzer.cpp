
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arsh/arsh.h>

enum class FuzzPolicy {
  EVAL, // default
  COMPLETE,
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
  return FuzzPolicy::EVAL;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static auto policy = getFuzzPolicy();

  auto *state = ARState_createWithMode(AR_EXEC_MODE_COMPILE_ONLY);
  switch (policy) {
  case FuzzPolicy::EVAL: {
    ARError dsError;
    ARState_eval(state, "<dummy>", (const char *)data, size, &dsError);
    ARError_release(&dsError);
    break;
  }
  case FuzzPolicy::COMPLETE: {
    std::string buf((const char *)data, size);
    const char *argv[] = {
        "complete",
        "-q",
        "--",
        buf.c_str(),
    };
    ARState_exec(state, (char **)argv);
    break;
  }
  }
  ARState_delete(&state);
  return 0;
}