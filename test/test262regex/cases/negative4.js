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
  phase: runtime
  type: SyntaxError
---*/

$DONOTEVALUATE();


//    CHECKERR: expected: negative(phase=runtime, type=SyntaxError)
//    CHECKERR: actual: [uncaught]
//    CHECKERR: Test262Error: Test262: This statement should not be evaluate
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/negative4\.js:17

// STATUS: 1