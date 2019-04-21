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

#include "server.h"

namespace ydsh {
namespace lsp {

// #######################
// ##     LSPServer     ##
// #######################

ReplyImpl LSPServer::onCall(const std::string &name, JSON &&param) {
    if(!this->init && name != "initialize") {
        this->logger(LogLevel::ERROR, "must be initialized");
        return newError(LSPErrorCode::ServerNotInitialized, "server not initialized!!");
    }
    return Handler::onCall(name, std::move(param));
}

static constexpr auto Type_ClientCapabilities =
        createInterface(
                "ClientCapabilities",
                field("workspace", !any),
                field("textDocument", !any)
        );

static constexpr auto Type_InitializeParams =
        createInterface(
                "InitializeParams",
                field("processId", integer),
                field("rootPath", !(string | null)),
                field("rootUri", string | null),
                field("initializationOptions", !any),
                field("capabilities", object(Type_ClientCapabilities)),
                field("trace", !string)
        );

void LSPServer::bindAll() {
    this->bind("shutdown", &LSPServer::shutdown);
    this->bind("exit", &LSPServer::exit);
    this->bind("initialize", Type_InitializeParams, &LSPServer::initialize);
}

void LSPServer::run() {
    while(true) {
        this->transport.dispatch(*this);
    }
}

Reply<InitializeResult> LSPServer::initialize(const InitializeParams &params) {
    this->logger(LogLevel::INFO, "initialize server ....");
    if(this->init) {
        return newError(ErrorCode::InvalidRequest, "server has already initialized");
    }
    this->init = true;

    (void) params; //FIXME: currently not used

    InitializeResult ret;   //FIXME: set supported capabilities
    return std::move(ret);
}

Reply<void> LSPServer::shutdown() {
    this->logger(LogLevel::INFO, "try to shutdown ....");
    this->willExit = true;
    return nullptr;
}

void LSPServer::exit() {
    int s = this->willExit ? 0 : 1;
    this->logger(LogLevel::INFO, "exit server: %d", s);
    std::exit(s);   // always success
}

} // namespace lsp
} // namespace ydsh