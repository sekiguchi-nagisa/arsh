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

namespace lsp {

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


// ##########################################
// ##     DiagnosticRelatedInformation     ##
// ##########################################

void fromJSON(JSON &&json, DiagnosticRelatedInformation &info) {
    fromJSON(std::move(json["location"]), info.location);
    info.message = std::move(json["message"].asString());
}

JSON toJSON(const DiagnosticRelatedInformation &info) {
    return {
        {"location", toJSON(info.location)},
        {"message", info.message}
    };
}

} // namespace lsp