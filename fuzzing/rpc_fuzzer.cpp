//
// Created by skgchxngsxyz-carbon on 18/11/16.
//

#include <stddef.h>
#include <stdint.h>

#include "jsonrpc.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  using namespace ydsh;
  using namespace rpc;

  ByteBuffer buf;
  buf.append(reinterpret_cast<const char *>(data), size);
  MessageParser parser(std::move(buf));
  auto ret = parser();
  if (parser.hasError()) {
    FILE *fp = fopen("/dev/null", "w");
    parser.showError(fp);
    fclose(fp);
  }
  (void)ret;
  return 0;
}
