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

for (var i = 0; i < 3; i++) {
    console.log(i);
}
for (; i < 5; ++i) {
    let v = "%" + i;
    console.log(v);
}

// CHECK: []
// CHECK: [ 0 ]
// CHECK: [ 0, 1 ]
// CHECK: [ 0, 1, 2 ]
// CHECK: [ 0, 1, 2, 3 ]
// CHECK: 0
// CHECK: 1
// CHECK: 2
// CHECK: %3
// CHECK: %4
// CHECKERR_RE: ^$
// STATUS: 0