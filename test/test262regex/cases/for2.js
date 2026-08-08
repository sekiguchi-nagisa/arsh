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

for (let aa = []; aa.length < 5; aa.push(aa.length)) {
    console.log(aa);
}

// CHECK: []
// CHECK: [ 0 ]
// CHECK: [ 0, 1 ]
// CHECK: [ 0, 1, 2 ]
// CHECK: [ 0, 1, 2, 3 ]
// CHECKERR_RE: ^$
// STATUS: 0