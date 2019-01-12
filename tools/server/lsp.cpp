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

#include "lsp.h"
#include "../json/conv.hpp"

namespace ydsh {
namespace rpc {

#define FROM_JSON(json, value, field) fromJSON(std::move(json[#field]), value.field)
#define FROM_JSON_OPT(json, value, field) fromJSON(json, #field, value.field)
#define MOVE_JSON(json, value, field) value.field = std::move(json[#field])

#define TO_MEMBER(obj, field) {#field, toJSON(obj.field)}

template <typename T>
static void fromJSON(JSON &json, const char *field, Union<T> &value) {
    auto v = std::move(json[field]);
    if(!v.isInvalid()) {
        T t;
        fromJSON(std::move(v), t);
        value = std::move(t);
    }
}

void fromJSON(JSON &&json, DocumentURI &uri) {
    fromJSON(std::move(json), uri.uri);
}

JSON toJSON(const DocumentURI &uri) {
    return JSON(uri.uri);
}


void fromJSON(JSON &&json, Position &p) {
    p.line = json["line"].asLong();
    p.character = json["character"].asLong();
}

JSON toJSON(const Position &p) {
    return {
        TO_MEMBER(p, line),
        TO_MEMBER(p, character)
    };
}


void fromJSON(JSON &&json, Range &range) {
    FROM_JSON(json, range, start);
    FROM_JSON(json, range, end);
}

JSON toJSON(const Range &range) {
    return {
        TO_MEMBER(range, start),
        TO_MEMBER(range, end)
    };
}


void fromJSON(JSON &&json, Location &location) {
    FROM_JSON(json, location, range);
    FROM_JSON(json, location, uri);
}

JSON toJSON(const Location &location) {
    return {
        TO_MEMBER(location, uri),
        TO_MEMBER(location, range)
    };
}


void fromJSON(JSON &&json, LocationLink &link) {
    FROM_JSON_OPT(json, link, originSelectionRange);
    FROM_JSON(json, link, targetUri);
    FROM_JSON(json, link, targetRange);
    FROM_JSON_OPT(json, link, targetSelectionRange);
}

JSON toJSON(const LocationLink &link) {
    return {
        TO_MEMBER(link, originSelectionRange),
        TO_MEMBER(link, targetUri),
        TO_MEMBER(link, targetRange),
        TO_MEMBER(link, targetSelectionRange)
    };
}


void fromJSON(JSON &&json, DiagnosticRelatedInformation &info) {
    FROM_JSON(json, info, message);
    FROM_JSON(json, info, location);
}

JSON toJSON(const DiagnosticRelatedInformation &info) {
    return {
        TO_MEMBER(info, location),
        TO_MEMBER(info, message)
    };
}


void fromJSON(JSON &&json, Diagnostic &diagnostic) {
    FROM_JSON(json, diagnostic, range);

    auto v = std::move(json["severity"]);
    diagnostic.severity = v.isInvalid() ? DiagnosticSeverity::DUMMY : static_cast<DiagnosticSeverity>(v.asLong());
    FROM_JSON(json, diagnostic, message);
    FROM_JSON_OPT(json, diagnostic, relatedInformation);
}

JSON toJSON(const Diagnostic &diagnostic) {
    auto severity = static_cast<int>(diagnostic.severity);

    return {
        TO_MEMBER(diagnostic, range),
        {"severity", severity > 0 ? JSON(severity): JSON()},
        //{"code"}
        //{"source"}
        TO_MEMBER(diagnostic, message),
        TO_MEMBER(diagnostic, relatedInformation)
    };
}


void fromJSON(JSON &&json, Command &command) {
    FROM_JSON(json, command, title);
    FROM_JSON(json, command, command);
}

JSON toJSON(const Command &command) {
    return {
        TO_MEMBER(command, title),
        TO_MEMBER(command, command)
    };
}


void fromJSON(JSON &&json, TextEdit &edit) {
    FROM_JSON(json, edit, range);
    FROM_JSON(json, edit, newText);
}

JSON toJSON(const TextEdit &edit) {
    return {
        TO_MEMBER(edit, range),
        TO_MEMBER(edit, newText)
    };
}


void fromJSON(JSON &&json, TraceSetting &setting) {
    std::string value;
    fromJSON(std::move(json), value);
    setting = TraceSetting::off;
    if(value == "off") {
        setting = TraceSetting::off;
    } else if(value == "messages") {
        setting = TraceSetting::messages;
    } else if(value == "verbose") {
        setting = TraceSetting::verbose;
    }
}

JSON toJSON(TraceSetting setting) {
    JSON value;
    switch(setting) {
    case TraceSetting::off:
        value = JSON("off");
        break;
    case TraceSetting::messages:
        value = JSON("messages");
        break;
    case TraceSetting::verbose:
        value = JSON("verbose");
        break;
    }
    return value;
}


void fromJSON(JSON &&json, ClientCapabilities &cap) {
    MOVE_JSON(json, cap, workspace);
    MOVE_JSON(json, cap, textDocument);
}

JSON toJSON(const ClientCapabilities &cap) {
    return {
        TO_MEMBER(cap, workspace),
        TO_MEMBER(cap, textDocument)
    };
}


void fromJSON(JSON &&json, InitializeParams &params) {
    auto value = std::move(json["processId"]);
    if(value.isLong()) {
        int v = value.asLong();
        params.processId = v;
    } else if(value.isNull()) {
        params.processId = nullptr;
    }

    value = std::move(json["rootPath"]);
    if(value.isString()) {
        params.rootPath = std::move(value.asString());
    } else if(value.isNull()) {
        params.rootPath = nullptr;
    }

    value = std::move(json["rootUri"]);
    if(value.isString()) {
        DocumentURI uri;
        fromJSON(std::move(value), uri);
        params.rootUri = std::move(uri);
    } else if(value.isNull()) {
        params.rootUri = nullptr;
    }

    MOVE_JSON(json, params, initializationOptions);
    FROM_JSON(json, params, capabilities);
    FROM_JSON_OPT(json, params, trace);
}

JSON toJSON(const InitializeParams &params) {
    return {
        TO_MEMBER(params, processId),
        TO_MEMBER(params, rootPath),
        TO_MEMBER(params, rootUri),
        TO_MEMBER(params, initializationOptions),
        TO_MEMBER(params, capabilities),
        TO_MEMBER(params, trace)
    };
}

} // namespace rpc
} // namespace ydsh