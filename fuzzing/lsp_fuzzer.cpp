//
// Created by skgchxngsxyz-leap on 2019/12/28.
//

#include <stddef.h>
#include <stdint.h>

#include "server.h"
#include <misc/files.h>

using namespace ydsh;
using namespace lsp;

static auto openDevNull() { return createFilePtr(fopen, "/dev/null", "wb"); }

static FilePtr newRequest(TempFileFactory &factory, const char *data, unsigned int size) {
  std::string inputName;
  auto input = factory.createTempFilePtr(inputName, (const char *)data, size);

  LSPLogger logger;
  LSPTransport transport(logger, openDevNull(), std::move(input));
  transport.send(size, data);

  return createFilePtr(fopen, inputName.c_str(), "rb");
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  TempFileFactory factory("ydsh_lsp_fuzzer");
  auto req = newRequest(factory, (const char *)data, size);

  LSPLogger logger;
  logger.setSeverity(LogLevel::INFO);
  logger.setAppender(openDevNull());
  LSPServer server(logger, std::move(req), openDevNull(), 1000);
  server.runOnlyOnce();
  return 0;
}