// RUN: exec $cmd $self

/*---
author: dummy
description: >
  sample test case
info: |
  sample
esid: sample
---*/

const actual = ['hey', 0x3093, true, null, assert.compareArray];
const expected = ["hey", 0x3093, true, null, assert.compareArray];
assert.compareArray(actual, expected);

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0