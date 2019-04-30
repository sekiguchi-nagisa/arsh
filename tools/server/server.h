/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_SERVER_H
#define YDSH_TOOLS_SERVER_H

#include "lsp.h"
#include "transport.h"
#include "../json/jsonrpc.h"

namespace ydsh {
namespace lsp {

using namespace json;
using namespace rpc;

struct LSPLogger : public LoggerBase {
    LSPLogger() : LoggerBase("YDSHD") {}
};

class LSPServer : public Handler {
private:
    LSPTransport transport;
    bool init{false};
    bool willExit{false};

public:
    LSPServer(FilePtr &&in, FilePtr &&out, LoggerBase &logger) :
        Handler(logger), transport(logger, std::move(in), std::move(out)) {
        this->bindAll();
    }

    const LSPTransport &getTransport() const {
        return this->transport;
    }

    ReplyImpl onCall(const std::string &name, JSON &&param) override;

    bool runOnlyOnce() {
        return this->transport.dispatch(*this);
    }

    [[noreturn]] void run();

private:
    /**
     * bind all of methods
     */
    void bindAll();

    template<typename Ret, typename Param>
    void bind(const std::string &name, Reply<Ret>(LSPServer::*method)(const Param &)) {
        Handler::bind(name, this, method);
    }

    template <typename Ret>
    void bind(const std::string &name, Reply<Ret>(LSPServer::*method)()) {
        Handler::bind(name, this, method);
    }

    template<typename Param>
    void bind(const std::string &name, void(LSPServer::*method)(const Param &)) {
        Handler::bind(name, this, method);
    }

    void bind(const std::string &name, void(LSPServer::*method)()) {
        Handler::bind(name, this, method);
    }

public:
    // RPC method definition
    Reply<InitializeResult> initialize(const InitializeParams &params);

    Reply<void> shutdown();

    void exit();
};


} // namespace lsp
} // namespace ydsh

#endif //YDSH_TOOLS_SERVER_H
