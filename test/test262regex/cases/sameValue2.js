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

assert.sameValue(matchSymbols, '@*ぁあぃいァアィ', 'failed,');

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: failed, Expected SameValue(«@*ぁあぃいァアィイ», «@*ぁあぃいァアィ») to be true
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/sameValue2\.js:23

// STATUS: 1