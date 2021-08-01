//
// Created by skgchxngsxyz-carbon on 18/11/16.
//

#include <stddef.h>
#include <stdint.h>

#include "jsonrpc.h"

struct NullLogger : public ydsh::LoggerBase {
  NullLogger() : LoggerBase("") {}
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  using namespace ydsh;
  using namespace rpc;

  ::NullLogger logger;
  ByteBuffer buf;
  buf.append(reinterpret_cast<const char *>(data), size);
  MessageParser parser(logger, std::move(buf));
  auto ret = parser();
  if (parser.hasError()) {
    FILE *fp = fopen("/dev/null", "w");
    parser.showError(fp);
    fclose(fp);
  }
  (void)ret;
  return 0;
}
