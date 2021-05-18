//
// Created by skgchxngsxyz-leap on 2021/02/10.
//

#include <stddef.h>
#include <stdint.h>
#include <string>

#include <ydsh/ydsh.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  auto *state = DSState_createWithMode(DS_EXEC_MODE_COMPILE_ONLY);
  std::string buf((const char *)data, size);
  const char *ptr = buf.c_str();
  DSState_complete(state, DS_COMP_INVOKE, size, &ptr);
  DSState_delete(&state);
  return 0;
}