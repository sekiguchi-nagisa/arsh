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

#ifndef TOOLS_LSP_H
#define TOOLS_LSP_H

#include "../json/json.h"

namespace lsp {

using namespace json;

// definition of basic interface of language server protocol

struct DocumentUri {
    std::string value;
};

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
    DocumentUri uri;    //FIXME
    Range range;
};

void fromJSON(JSON &&json, Location &location);
JSON toJSON(const Location &location);


enum class DiagnosticSeverity {
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

} // namespace lsp

#endif //TOOLS_LSP_H
