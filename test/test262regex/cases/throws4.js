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

assert.throws(Test262Error, function () {
    throw new Test262Error('failed');
}, "failed");

console.log('hey');

// CHECK: hey
// CHECKERR_RE: ^$

// STATUS: 0