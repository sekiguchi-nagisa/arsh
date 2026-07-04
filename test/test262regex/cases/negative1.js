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
negative:
  phase: parse
  type: SyntaxError
---*/

assert(true);

// CHECKERR: expected: negative(phase=parse, type=SyntaxError)
// CHECKERR: actual: undefined

// STATUS: 1