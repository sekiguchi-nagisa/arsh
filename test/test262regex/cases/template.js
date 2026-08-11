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

assert.sameValue("", ``);
assert.sameValue("$$", `$$`);
assert.sameValue("$$\{", `$$\{`);
assert.sameValue("<undefined>", `<${undefined}>`);
assert.sameValue("<100,false>", `<${23 + 77},${`hey` === `hey!`}>`);

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0