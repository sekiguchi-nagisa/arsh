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
        "\u{1F468}\u{1F3FC}\u200D\u2764\uFE0F\u200D\u{1F48B}\u200D\u{1F468}\u{1F3FF}",
        "\u{1FAF5}\u{1F3FB}",
    ],
    nonMatchStrings: [
        "\u{1FAF5}\u{1F3FB}",
    ],
});

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: `\p{RGI_Emoji}` should not match 🫵🏻 (U+01FAF5U+01F3FB)
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/testPropertyOfStrings_fail2\.js:14

// STATUS: 1