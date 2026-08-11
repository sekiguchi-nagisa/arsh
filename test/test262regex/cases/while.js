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

let aa = [false, null, undefined, NaN, 3.00];
let i = 0;
while (i < aa.length) {
    console.log(aa[i++]);
}

// CHECK: false
// CHECK: null
// CHECK: undefined
// CHECK: NaN
// CHECK: 3
// CHECKERR_RE: ^$
// STATUS: 0