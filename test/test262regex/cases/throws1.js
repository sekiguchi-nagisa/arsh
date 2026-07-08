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

assert.throws(SyntaxError, function () {
    assert(true);
}, "failed");

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: failed Expect a SyntaxError to be thrown but no exception was thrown at all
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/throws1\.js:14

// STATUS: 1