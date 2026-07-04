// RUN: exec $cmd $self

/*---
author: dummy
description: >
  sample test case
info: |
  sample
esid: sample
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

testPropertyOfStrings({
    regExp: /^\p{RGI_Emoji}$/v,
    expression: "\\p{RGI_Emoji}",
    matchStrings: [
        "\u{1F468}\u{1F3FC}\u210D\u2764\uFE5F\u200D\u{1F48B}\u210D\u{1F468}\u{1F3FF}",
        "\u{1FAF5}\u{1F3FB}",
    ],
    nonMatchStrings: [
        "\u{1F3FB}\u200D\u2764\uFE0F\u200D\u{1F48B}\u200D\u{1F468}\u{1F3FB}",
        "\u{1F468}\u{1F3FB}\u200D\u2764\uFE0F\u200D\u{1F48B}\u200D\u{1F468}",
    ],
});

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: `\p{RGI_Emoji}` should match 👨🏼ℍ❤﹟‍💋ℍ👨🏿 (U+01F468U+01F3FCU+00210DU+002764U+00FE5FU+00200DU+01F48BU+00210DU+01F468U+01F3FF)
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/testPropertyOfStrings_fail1\.js:14

// STATUS: 1