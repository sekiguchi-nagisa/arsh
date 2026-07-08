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

assert.throws(RegExp, function () {
    assert(false);
}, "failed");

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: failed Expected a RegExp but got a [object Object]
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/throws2\.js:14

// STATUS: 1