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

void fromJSON(JSON &&json, DocumentURI &uri);
JSON toJSON(const DocumentURI &uri);


struct Position {
    int line{0};
    int character{0};
};

void fromJSON(JSON &&json, Position &p);
JSON toJSON(const Position &p);


struct Range {
    Position start;
    Position end;
};

void fromJSON(JSON &&json, Range &range);
JSON toJSON(const Range &range);


struct Location {
    DocumentURI uri;
    Range range;
};

void fromJSON(JSON &&json, Location &location);
JSON toJSON(const Location &location);


struct LocationLink {
    Union<Range> originSelectionRange;  // optional
    std::string targetUri;
    Range targetRange;
    Union<Range> targetSelectionRange;  // optional
};

void fromJSON(JSON &&json, LocationLink &link);
JSON toJSON(const LocationLink &link);


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

void fromJSON(JSON &&json, DiagnosticRelatedInformation &info);
JSON toJSON(const DiagnosticRelatedInformation &info);


struct Diagnostic {
    Range range;
    DiagnosticSeverity severity{DiagnosticSeverity::DUMMY}; // optional
//    std::string code; // string | number, //FIXME: currently not supported.
//    std::string source;                   //FIXME: currently not supported.
    std::string message;
    std::vector<DiagnosticRelatedInformation> relatedInformation;   // optional
};

void fromJSON(JSON &&json, Diagnostic &diagnostic);
JSON toJSON(const Diagnostic &diagnostic);


struct Command {
    std::string title;
    std::string command;
//    std::vector<JSON> arguments // any[], optional  //FIXME: currently not supported.
};

void fromJSON(JSON &&json, Command &command);
JSON toJSON(const Command &command);


struct TextEdit {
    Range range;
    std::string newText;
};

void fromJSON(JSON &&json, TextEdit &edit);
JSON toJSON(const TextEdit &edit);


} // namespace lsp
} // namespace ydsh

#endif //YDSH_TOOLS_LSP_H
