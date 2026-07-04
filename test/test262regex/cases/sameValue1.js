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
            [0x3041, 0x3044], // あ, ん
            [0x0030A1, 0x0030A4]
        ]
    })
;

assert.sameValue(matchSymbols, '@*ぁあぃいァアィイ');

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0