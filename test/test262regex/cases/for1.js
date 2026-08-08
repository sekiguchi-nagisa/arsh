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

for (const a of "あいう") {
    console.log(a);
}

for (let aa of [1, false, null, undefined, NaN, "hey"]) {
    console.log(aa);
}

// CHECK: あ
// CHECK: い
// CHECK: う
// CHECK: 1
// CHECK: false
// CHECK: null
// CHECK: undefined
// CHECK: NaN
// CHECK: hey
// CHECKERR_RE: ^$
// STATUS: 0