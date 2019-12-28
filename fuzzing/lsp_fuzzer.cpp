//
// Created by skgchxngsxyz-leap on 2019/12/28.
//

#include <stdint.h>
#include <stddef.h>

#include "server.h"
#include <misc/files.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    using namespace ydsh;
    using namespace lsp;

    TempFileFactory factory;
    std::string inputName = "";
    auto input = factory.createTempFilePtr(inputName, (const char *)data, size);

    LSPLogger logger;
    logger.setSeverity(LogLevel::INFO);
    logger.setAppender(createFilePtr(fopen, "/dev/null", "wb"));
    LSPServer server(logger, std::move(input), createFilePtr(fopen, factory.getTempFileName(), "wb"));
    server.runOnlyOnce();
    return 0;
}