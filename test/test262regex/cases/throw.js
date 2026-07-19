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

throw new RangeError('too large number');
console.log('hey');

// CHECK_RE: ^$

//    CHECKERR: [uncaught]
//    CHECKERR: RangeError: too large number
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/throw\.js:14

// STATUS: 1