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

const r = console.log(12, false, null, undefined, /1234/u);

assert.sameValue(r, undefined);

// CHECK: 12 false null undefined /1234/u
// CHECKERR_RE: ^$
// STATUS: 0