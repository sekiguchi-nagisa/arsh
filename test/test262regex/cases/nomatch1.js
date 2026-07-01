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

const matchSymbols = buildString({
    loneCodePoints: [0x0040, 0x002A, 0x007c], // @, *, |
    ranges: [
        [0x3042, 0x3093] // あ, ん
    ]
});

testPropertyEscapes(
    /^[^]$/u,
    matchSymbols,
    "[^]"
);

testPropertyEscapes(
    /^[\u3042-\u3099@*]$/u,
    matchSymbols,
    "[\\u3042-\\u3099@*]"
);

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: `[\u3042-\u3099@*]` should match | (U+00007C)
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/nomatch1\.js:27

// STATUS: 1