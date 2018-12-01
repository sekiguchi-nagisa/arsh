//
// Created by skgchxngsxyz-carbon on 18/11/16.
//

#include <stdint.h>
#include <stddef.h>

#include "jsonrpc.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    using namespace ydsh;
    using namespace rpc;

    RequestParser parser;
    parser.append(reinterpret_cast<const char *>(data), size);
    auto ret = parser();
    if(parser.hasError()) {
        FILE *fp = fopen("/dev/null", "w");
        parser.showError(fp);
        fclose(fp);
    }
    (void) ret;
    return 0;
}

