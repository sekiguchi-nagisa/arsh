// RUN: exec $cmd -m $self

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

$DONOTEVALUATE();
assert(false);

// CHECK_RE: ^$
// CHECKERR_RE: ^$

// STATUS: 0