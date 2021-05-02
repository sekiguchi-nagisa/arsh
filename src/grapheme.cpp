/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#include <tuple>

#include "grapheme.h"
#include "misc/unicode.hpp"

namespace ydsh {

GraphemeBoundary::BreakProperty GraphemeBoundary::getBreakProperty(int codePoint) {
    using PropertyInterval = std::tuple<int, int, BreakProperty>;

#define UNICODE_PROPERTY_RANGE PropertyInterval
#define PROPERTY(E) BreakProperty::E
#include "grapheme_break_property.h"
#undef PROPERTY
#undef UNICODE_PROPERTY_RANGE

    struct Comp {
        bool operator()(const PropertyInterval &l, int r) const {
            return std::get<1>(l) < r;
        }

        bool operator()(int l, const PropertyInterval &r) const {
            return l < std::get<0>(r);
        }
    };

    auto iter = std::lower_bound(std::begin(grapheme_break_property_table),
                                 std::end(grapheme_break_property_table),
                                 codePoint, Comp());
    if(iter != std::end(grapheme_break_property_table)) {
        auto &interval = *iter;
        if(codePoint >= std::get<0>(interval) && codePoint <= std::get<1>(interval)) {
            return std::get<2>(interval);
        }
    }
    return BreakProperty::Any;
}


// see. https://unicode.org/reports/tr29/#Grapheme_Cluster_Boundary_Rules
bool GraphemeBoundary::scanBoundary(int codePoint) {
    auto after = getBreakProperty(codePoint);
    auto before = this->state;
    this->state = after;

    if(before == BreakProperty::SOT) {
        return false;
    }

    switch(before) {
    case BreakProperty::CR:
        if(after == BreakProperty::LF) {
            return false;   // GB3
        }
        return true;   // GB4
    case BreakProperty::LF:
    case BreakProperty::Control:
        return true;    // GB4
    default:
        switch(after) {
        case BreakProperty::Control:
        case BreakProperty::CR:
        case BreakProperty::LF:
            return true;    // GB5
        default:
            break;
        }
        break;
    }

    switch(before) {
    case BreakProperty::L:
        switch(after) {
        case BreakProperty::L:
        case BreakProperty::V:
        case BreakProperty::LV:
        case BreakProperty::LVT:
            return false;   // GB6
        default:
            break;
        }
        break;
    case BreakProperty::LV:
    case BreakProperty::V:
        if(after == BreakProperty::V || after == BreakProperty::T) {
            return false;   // GB7
        }
        break;
    case BreakProperty::LVT:
    case BreakProperty::T:
        if(after == BreakProperty::T) {
            return false;   // GB8
        }
        break;
    default:
        break;
    }

    switch(after) {
    case BreakProperty::Extend:
    case BreakProperty::ZWJ:
        if(before != BreakProperty::Extended_Pictographic) {
            return false;   // GB9
        }
        break;
    case BreakProperty::SpacingMark:
        return false;   // GB9a
    default:
        break;
    }

    switch(before) {
    case BreakProperty::Prepend:
        return false;   //GB9b
    case BreakProperty::Regional_Indicator:
        if(after == BreakProperty::Regional_Indicator) {
            this->state = BreakProperty::Any;
            return false;   // GB12, GB13
        }
        break;
    case BreakProperty::Extended_Pictographic:
        if(after == BreakProperty::Extend) {
            this->state = BreakProperty::Extended_Pictographic; // consume Extend
            return false;   // GB11
        } else if(after == BreakProperty::ZWJ) {
            this->state = BreakProperty::Extended_Pictographic_with_ZWJ;
            return false;   // GB11
        }
        break;
    case BreakProperty::Extended_Pictographic_with_ZWJ:
        if(after == BreakProperty::Extended_Pictographic){
            return false;   // GB11
        }
        break;
    default:
        break;
    }
    return true;    // GB999
}

static int toCodePoint(StringRef ref, size_t pos) {
    int codePoint = UnicodeUtil::utf8ToCodePoint(ref.begin() + pos, ref.end());
    if(codePoint < 0) {
        codePoint = ref[pos];   // broken encoding
    }
    return codePoint;
}

bool GraphemeScanner::next(Result &result) {
    result.codePointCount = this->prevPos == this->curPos ? 0 : 1;
    result.startPos = this->prevPos;
    result.byteSize = 0;
    result.firstCodePoint = result.codePointCount > 0 ? toCodePoint(this->ref, this->prevPos) : -1;

    while(this->curPos < this->ref.size()) {
        size_t pos = this->curPos;
        size_t nextPos = UnicodeUtil::utf8NextPos(this->curPos, this->ref[this->curPos]);
        int codePoint = toCodePoint(this->ref, this->curPos);
        if(result.codePointCount++ == 0) {
            result.firstCodePoint = codePoint;
        }
        this->curPos = nextPos;
        if(this->boundary.scanBoundary(codePoint)) {
            result.byteSize = pos - this->prevPos;
            this->prevPos = pos;
            return true;
        }
    }
    if(this->curPos == this->ref.size()) {
        result.byteSize = this->curPos - this->prevPos;
        this->prevPos = this->curPos;
    }
    return result.codePointCount > 0;
}

} // namespace ydsh