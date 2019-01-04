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

#include "lsp.h"

namespace ydsh {
namespace lsp {

#define FROM_JSON(json, value, field) fromJSON(std::move(json[#field]), value.field)

static void fromJSON(JSON &&json, std::string &value) {
    value = std::move(json.asString());
}

// #########################
// ##     DocumentURI     ##
// #########################

void fromJSON(JSON &&json, DocumentURI &uri) {
    fromJSON(std::move(json), uri.uri);
}

JSON toJSON(const DocumentURI &uri) {
    return JSON(uri.uri);
}

// ######################
// ##     Position     ##
// ######################

void fromJSON(JSON &&json, Position &p) {
    p.line = json["line"].asLong();
    p.character = json["character"].asLong();
}

JSON toJSON(const Position &p) {
    return {
        {"line", p.line},
        {"character", p.character}
    };
}

// ###################
// ##     Range     ##
// ###################

void fromJSON(JSON &&json, Range &range) {
    FROM_JSON(json, range, start);
    FROM_JSON(json, range, end);
}

JSON toJSON(const Range &range) {
    return {
        {"start", toJSON(range.start)},
        {"end", toJSON(range.end)}
    };
}

// ######################
// ##     Location     ##
// ######################

void fromJSON(JSON &&json, Location &location) {
    FROM_JSON(json, location, range);
    FROM_JSON(json, location, uri);
}

JSON toJSON(const Location &location) {
    return {
        {"uri", toJSON(location.uri)},
        {"range", toJSON(location.range)}
    };
}

// ##########################
// ##     LocationLink     ##
// ##########################

void fromJSON(JSON &&json, LocationLink &link) {
    auto v = std::move(json["originSelectionRange"]);
    if(!v.isInvalid()) {
        Range range;
        fromJSON(std::move(v), range);
        link.originSelectionRange = std::move(range);
    }
    link.targetUri = std::move(json["targetUri"].asString());
    FROM_JSON(json, link, targetRange);

    v = std::move(json["targetSelectionRange"]);
    if(!v.isInvalid()) {
        Range range;
        fromJSON(std::move(v), range);
        link.targetSelectionRange = std::move(range);
    }
}

JSON toJSON(const LocationLink &link) {
    return {
        {"originSelectionRange", link.originSelectionRange.hasValue() ? toJSON(get<Range>(link.originSelectionRange)) : JSON()},
        {"targetUri", JSON(link.targetUri)},
        {"targetRange", toJSON(link.targetRange)},
        {"targetSelectionRange", link.targetSelectionRange.hasValue() ? toJSON(get<Range>(link.targetSelectionRange)) : JSON()}
    };
}

// ##########################################
// ##     DiagnosticRelatedInformation     ##
// ##########################################

void fromJSON(JSON &&json, DiagnosticRelatedInformation &info) {
    FROM_JSON(json, info, message);
    FROM_JSON(json, info, location);
}

JSON toJSON(const DiagnosticRelatedInformation &info) {
    return {
        {"location", toJSON(info.location)},
        {"message", info.message}
    };
}

// ########################
// ##     Diagnostic     ##
// ########################

void fromJSON(JSON &&json, Diagnostic &diagnostic) {
    FROM_JSON(json, diagnostic, range);

    auto v = std::move(json["severity"]);
    diagnostic.severity = v.isInvalid() ? DiagnosticSeverity::DUMMY : static_cast<DiagnosticSeverity>(v.asLong());
    FROM_JSON(json, diagnostic, message);

    v = std::move(json["relatedInformation"]);
    if(!v.isInvalid()) {
        for(auto &e : v.asArray()) {
            DiagnosticRelatedInformation info;
            fromJSON(std::move(e), info);
            diagnostic.relatedInformation.push_back(std::move(info));
        }
    }
}

JSON toJSON(const Diagnostic &diagnostic) {
    int severity = static_cast<int>(diagnostic.severity);

    json::Array jsonArray;
    for(auto &e : diagnostic.relatedInformation) {
        jsonArray.emplace_back(toJSON(e));
    }

    return {
        {"range", toJSON(diagnostic.range)},
        {"severity", severity > 0 ? JSON(severity): JSON()},
        //{"code"}
        //{"source"}
        {"message", JSON(diagnostic.message)},
        {"relatedInformation", jsonArray.empty() ? JSON() : JSON(std::move(jsonArray))}
    };
}

// #####################
// ##     Command     ##
// #####################

void fromJSON(JSON &&json, Command &command) {
    FROM_JSON(json, command, title);
    FROM_JSON(json, command, command);
}

JSON toJSON(const Command &command) {
    return {
        {"title", JSON(command.title)},
        {"command", JSON(command.command)}
    };
}

// ######################
// ##     TextEdit     ##
// ######################

void fromJSON(JSON &&json, TextEdit &edit) {
    FROM_JSON(json, edit, range);
    FROM_JSON(json, edit, newText);
}

JSON toJSON(const TextEdit &edit) {
    return {
        {"range", toJSON(edit.range)},
        {"newText", JSON(edit.newText)}
    };
}


} // namespace lsp
} // namespace ydsh