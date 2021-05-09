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

namespace ydsh::lsp {

// #######################
// ##     LSPServer     ##
// #######################

#define LOG(L, ...) (this->logger.get())(L, __VA_ARGS__)

ReplyImpl LSPServer::onCall(const std::string &name, JSON &&param) {
    if(!this->init && name != "initialize") {
        LOG(LogLevel::ERROR, "must be initialized");
        return newError(LSPErrorCode::ServerNotInitialized, "server not initialized!!");
    }
    return Handler::onCall(name, std::move(param));
}

void LSPServer::bindAll() {
    this->bind("shutdown", &LSPServer::shutdown);
    this->bind("exit", &LSPServer::exit);
    this->bind("initialize", &LSPServer::initialize);
    this->bind("initialized", &LSPServer::initialized);
    this->bind("textDocument/didOpen", &LSPServer::didOpenTextDocument);
    this->bind("textDocument/didClose", &LSPServer::didCloseTextDocument);
}

void LSPServer::run() {
    while(true) {
        this->runOnlyOnce();
    }
}

Reply<InitializeResult> LSPServer::initialize(const InitializeParams &params) {
    LOG(LogLevel::INFO, "initialize server ....");
    if(this->init) {
        return newError(ErrorCode::InvalidRequest, "server has already initialized");
    }
    this->init = true;

    (void) params; //FIXME: currently not used

    InitializeResult ret;   //FIXME: set supported capabilities
    ret.capabilities.textDocumentSync = TextDocumentSyncOptions {
        .openClose = true,
        .change = {},
        .willSave = {},
        .willSaveWaitUntil = {},
        .save = {},
    };
    return std::move(ret);
}

void LSPServer::initialized(const ydsh::lsp::InitializedParams &) {
    LOG(LogLevel::INFO, "server initialized!!");
}

Reply<void> LSPServer::shutdown() {
    LOG(LogLevel::INFO, "try to shutdown ....");
    this->willExit = true;
    return nullptr;
}

void LSPServer::exit() {
    int s = this->willExit ? 0 : 1;
    LOG(LogLevel::INFO, "exit server: %d", s);
    std::exit(s);   // always success
}

void LSPServer::didOpenTextDocument(const DidOpenTextDocumentParams &params) {
    LOG(LogLevel::INFO, "open textDocument: %s", params.textDocument.uri.c_str());
}

void LSPServer::didCloseTextDocument(const DidCloseTextDocumentParams &params) {
    LOG(LogLevel::INFO, "close textDocument: %s", params.textDocument.uri.c_str());
}

} // namespace ydsh::lsp