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

const hoge;

// CHECKERR_RE: .+\/test\/test262regex\/cases\/syntax_error\.js:17 \[error\] mismatched token `;', expected `='
//    CHECKERR: const hoge;
//    CHECKERR:           ^

// STATUS: 0