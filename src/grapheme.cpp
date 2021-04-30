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

GraphemeCluster::BreakProperty GraphemeCluster::getBreakProperty(int codePoint) {
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

} // namespace ydsh