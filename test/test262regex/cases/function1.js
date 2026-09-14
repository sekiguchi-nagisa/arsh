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

function even(n) {
    if (n === 0) {
        return true
    }
    return odd(n - 1)
}

function odd(n) {
    if (n === 0) {
        return false
    }
    return even(n - 1)
}

console.log(even(12));
console.log(even(13));
console.log(odd(14));
console.log(odd(15));

// CHECK: true
// CHECK: false
// CHECK: false
// CHECK: true
// CHECKERR_RE: ^$
// STATUS: 0