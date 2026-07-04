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

assert(true;


// CHECKERR_RE: .+\/test\/test262regex\/cases\/negative2\.js:17 \[error\] mismatched token `;', expected `,', `\)'
//    CHECKERR: assert(true;
//    CHECKERR:            ^
//    CHECKERR: expected: phase=runtime
//    CHECKERR: actual: phase=parse

// STATUS: 1