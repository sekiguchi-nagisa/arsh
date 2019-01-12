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

#ifndef YDSH_TOOLS_LSP_H
#define YDSH_TOOLS_LSP_H

#include "../json/json.h"
#include "../uri/uri.h"

namespace ydsh {
namespace lsp {

using namespace json;

// definition of basic interface of language server protocol

// LSP specific error code
constexpr int ServerErrorStart     = -32099;
constexpr int ServerErrorEnd       = -32000;
constexpr int ServerNotInitialized = -32002;
constexpr int UnknownErrorCode     = -32001;

constexpr int RequestCancelled     = -32800;
constexpr int ContentModified      = -32801;

struct DocumentURI {
    std::string uri;    // must be valid URI
};

struct Position {
    int line{0};
    int character{0};
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    DocumentURI uri;
    Range range;
};

struct LocationLink {
    Union<Range> originSelectionRange;  // optional
    std::string targetUri;
    Range targetRange;
    Union<Range> targetSelectionRange;  // optional
};


enum class DiagnosticSeverity : int {
    DUMMY = 0,  // not defined in protocol
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

struct DiagnosticRelatedInformation {
    Location location;
    std::string message;
};

struct Diagnostic {
    Range range;
    DiagnosticSeverity severity{DiagnosticSeverity::DUMMY}; // optional
//    std::string code; // string | number, //FIXME: currently not supported.
//    std::string source;                   //FIXME: currently not supported.
    std::string message;
    Union<std::vector<DiagnosticRelatedInformation>> relatedInformation;   // optional
};

struct Command {
    std::string title;
    std::string command;
//    std::vector<JSON> arguments // any[], optional  //FIXME: currently not supported.
};

struct TextEdit {
    Range range;
    std::string newText;
};

// for Initialize request

struct ClientCapabilities {
    JSON workspace;   // optional
    JSON textDocument; // optional
};

enum class TraceSetting {
    off,
    messages,
    verbose
};

struct InitializeParams {
    Union<int, std::nullptr_t> processId;
    Union<std::string, std::nullptr_t> rootPath;    // optional
    Union<DocumentURI, std::nullptr_t> rootUri;
    JSON initializationOptions; // optional
    ClientCapabilities capabilities;
    Union<TraceSetting> trace;  // optional
//    Union<WorkspaceFolder, std::nullptr_t> workspaceFolders;    // optional   //FIXME: currently not supported
};


} // namespace lsp

namespace rpc {

using namespace lsp;

void fromJSON(JSON &&json, DocumentURI &uri);
JSON toJSON(const DocumentURI &uri);

void fromJSON(JSON &&json, Position &p);
JSON toJSON(const Position &p);

void fromJSON(JSON &&json, Range &range);
JSON toJSON(const Range &range);

void fromJSON(JSON &&json, Location &location);
JSON toJSON(const Location &location);

void fromJSON(JSON &&json, LocationLink &link);
JSON toJSON(const LocationLink &link);

void fromJSON(JSON &&json, DiagnosticRelatedInformation &info);
JSON toJSON(const DiagnosticRelatedInformation &info);

void fromJSON(JSON &&json, Diagnostic &diagnostic);
JSON toJSON(const Diagnostic &diagnostic);

void fromJSON(JSON &&json, Command &command);
JSON toJSON(const Command &command);

void fromJSON(JSON &&json, TextEdit &edit);
JSON toJSON(const TextEdit &edit);

void fromJSON(JSON &&json, TraceSetting &setting);
JSON toJSON(TraceSetting setting);

void fromJSON(JSON &&json, ClientCapabilities &cap);
JSON toJSON(const ClientCapabilities &cap);

void fromJSON(JSON &&json, InitializeParams &params);
JSON toJSON(const InitializeParams &params);

} // namespace rpc
} // namespace ydsh

#endif //YDSH_TOOLS_LSP_H
