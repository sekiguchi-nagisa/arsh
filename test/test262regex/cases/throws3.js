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
    new RegExp(/234/i, 'G');
    assert(false);
}, "failed");

// CHECK_RE: ^$
// CHECKERR_RE: ^$

// STATUS: 0