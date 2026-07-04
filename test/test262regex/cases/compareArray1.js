// RUN: exec $cmd $self

/*---
author: dummy
description: >
  sample test case
info: |
  sample
esid: sample
---*/

const actual = [0x3042, 0x3093];
const expected = [0x3042, 0x3093];
assert.compareArray(actual, expected);

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0