//
// Created by skgchxngsxyz-carbon on 16/07/31.
//

#include <stdint.h>
#include <stddef.h>

#include <ydsh/ydsh.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    auto *state = DSState_createWithMode(DS_EXEC_MODE_COMPILE_ONLY);
    DSError dsError;
    DSState_eval(state, "<dummy>", (const char *)data, size, &dsError);
    DSError_release(&dsError);
    DSState_delete(&state);
    return 0;
}