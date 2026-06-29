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
        [0x3041, 0x3044] // あ, ん
    ]
});

assert.notSameValue(matchSymbols, '@*ぁあぃえい', 'failed,');

// CHECK_RE: ^$
// CHECKERR_RE: ^$

// STATUS: 0