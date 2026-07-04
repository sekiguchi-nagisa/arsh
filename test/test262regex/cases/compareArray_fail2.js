// RUN: exec $cmd $self

/*---
author: dummy
description: >
  sample test case
info: |
  sample
esid: sample
---*/

const actual = [0x3042, 0xfffe];
const expected = [0x3042, 0x3093];
assert.compareArray(actual, expected, "need same codes");

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: Actual [ 12354, 65534 ] and Expected [ 12354, 12435 ] should have same content. need same codes
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/compareArray_fail2\.js:14


// STATUS: 1