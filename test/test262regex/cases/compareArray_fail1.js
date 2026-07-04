// RUN: exec $cmd $self

/*---
author: dummy
description: >
  sample test case
info: |
  sample
esid: sample
---*/

const actual = [0x3042, 0x3093, 0xfffe];
const expected = [0x3042, 0x3093];
assert.compareArray(actual, expected);

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: Actual [ 12354, 12435, 65534 ] and Expected [ 12354, 12435 ] should have same content.
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/compareArray_fail1\.js:14


// STATUS: 1