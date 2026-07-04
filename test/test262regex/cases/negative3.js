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

RegExp('1234', '6');

//    CHECKERR: expected: phase=parse
//    CHECKERR: actual: phase=runtime

// STATUS: 1