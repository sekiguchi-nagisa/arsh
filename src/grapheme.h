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

#ifndef YDSH_GRAPHEME_H
#define YDSH_GRAPHEME_H

namespace ydsh {

// for unicode grapheme cluster support

class GraphemeBoundary {
public:
    // for grapheme cluster boundry. only support extended grapheme cluster
    enum class BreakProperty {
        SOT,    // for GB1

        Any,
        CR,
        LF,
        Control,
        Extend,
        ZWJ,
        Regional_Indicator,
        Prepend,
        SpacingMark,
        L,
        V,
        T,
        LV,
        LVT,

        Extended_Pictographic,

        Extended_Pictographic_with_ZWJ, // indicates \p{Extended_Pictographic} Extend* ZWJ
    };

    static BreakProperty getBreakProperty(int codePoint);

private:
    /**
     * may be indicate previous code point property
     */
    BreakProperty state{BreakProperty::SOT};

public:
    GraphemeBoundary() = default;

    explicit GraphemeBoundary(BreakProperty init) : state(init) {}

    /**
     * scan grapheme cluster boundary
     * @param codePoint
     * @return
     * if grapheme cluster boundary is between prev codePoint and codePoint, return true
     */
    bool scanBoundary(int codePoint);
};

} // namespace ydsh

#endif //YDSH_GRAPHEME_H
