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

let a = () => 12;
assert.sameValue(a(), 12);

let b = (a) => a + a;
assert.sameValue(b(10), 20);

let c = (a, b,) => a + b;
assert.sameValue(c(10, 11), 21);

// CHECK_RE: ^$
// CHECK_ERR_RE: ^$

// STATUS: 0