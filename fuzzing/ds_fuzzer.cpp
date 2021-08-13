//
// Created by skgchxngsxyz-carbon on 16/07/31.
//

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <ydsh/ydsh.h>

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

  if (const char *env = getenv("YDSH_FUZZ_POLICY"); env && *env) {
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

  auto *state = DSState_createWithMode(DS_EXEC_MODE_COMPILE_ONLY);
  switch (policy) {
  case FuzzPolicy::EVAL: {
    DSError dsError;
    DSState_eval(state, "<dummy>", (const char *)data, size, &dsError);
    DSError_release(&dsError);
    break;
  }
  case FuzzPolicy::COMPLETE: {
    const char *buf = (const char *)data;
    DSState_complete(state, DS_COMP_INVOKE, size, &buf);
    break;
  }
  }
  DSState_delete(&state);
  return 0;
}