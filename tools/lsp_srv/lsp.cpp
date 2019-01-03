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

// #########################
// ##     DocumentURI     ##
// #########################

void fromJSON(JSON &&json, DocumentURI &uri) {
    uri.uri = std::move(json.asString());
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
        {"character", p.line}
    };
}

// ###################
// ##     Range     ##
// ###################

void fromJSON(JSON &&json, Range &range) {
    fromJSON(std::move(json["start"]), range.start);
    fromJSON(std::move(json["end"]), range.end);
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
    fromJSON(std::move(json["rage"]), location.range);
    fromJSON(std::move(json["uri"]), location.uri);
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
    fromJSON(std::move(json["targetRange"]), link.targetRange);

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
    info.message = std::move(json["message"].asString());
    fromJSON(std::move(json["location"]), info.location);
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
    fromJSON(std::move(json["range"]), diagnostic.range);

    auto v = std::move(json["severity"]);
    diagnostic.severity = v.isInvalid() ? DiagnosticSeverity::DUMMY : static_cast<DiagnosticSeverity>(v.asLong());
    diagnostic.message = std::move(json["message"].asString());

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
    command.title = std::move(json["title"].asString());
    command.command = std::move(json["command"].asString());
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
    fromJSON(std::move(json["range"]), edit.range);
    edit.newText = std::move(json["newText"].asString());
}

JSON toJSON(const TextEdit &edit) {
    return {
        {"range", toJSON(edit.range)},
        {"newText", JSON(edit.newText)}
    };
}


} // namespace lsp
} // namespace ydsh