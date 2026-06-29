// RUN: exec $cmd -d $self

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

assert(true);

// CHECKERR: --- meta-data ---
// CHECKERR: author: dummy
// CHECKERR: description: sample test case
// CHECKERR: info: sample
// CHECKERR: esid: sample
// CHECKERR: features: [regexp-unicode-property-escapes]
// CHECKERR: includes: [regExpUtils.js]
// CHECKERR: (<Identifier>, assert)
// CHECKERR: ((, ()
// CHECKERR: (true, true)
// CHECKERR: (), ))
// CHECKERR: (;, ;)
// CHECKERR: (<EOS>, )
// CHECKERR: undefined

// CHECK_RE: ^$

// STATUS: 0