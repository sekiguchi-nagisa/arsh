/*
 * Copyright (C) 2018 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef YDSH_TOOLS_TRANSPORT_H
#define YDSH_TOOLS_TRANSPORT_H

#include "../json/jsonrpc.h"

namespace ydsh {
namespace lsp {

using namespace json;

class LSPTransport : public rpc::Transport {
private:
    FilePtr input;
    FilePtr output;

public:
    LSPTransport(LoggerBase &logger, FILE *in, FILE *out) :
        rpc::Transport(logger),input(in), output(out) {}

    ~LSPTransport() override = default;

private:
    int send(unsigned int size, const char *data) override;

    /**
     *
     * @return
     * return -1, if cannot read message size
     * return 0, may be broken header
     */
    int recvSize() override;

    int recv(unsigned int size, char *data) override;

    /**
     * if reach end of header, set empty string
     * @return
     */
    bool readHeader(std::string &header);
};


} // namespace lsp
} // namespace ydsh

#endif //YDSH_TOOLS_TRANSPORT_H
