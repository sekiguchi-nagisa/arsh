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
    loneCodePoints: [0x0040, 0x002A], // @, *
    ranges: [
        [0x3042, 0x3093] // あ, ん
    ]
});

testPropertyEscapes(
    /^[\u3042-\u3099@*]$/u,
    matchSymbols,
    "[\\u3042-\\u3099@*]"
);

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0