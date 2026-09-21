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

let func = function (a) {
    switch (a) {
        case 2:
            console.log(2);
            break;
        default:
            console.log('default');
        case 1:
            console.log(1);
    }
};

let func2 = function (a) {
    switch (a) {
        case console.log(11):
        case console.log(22):
    }
}

func(1)
func(2)
func(3)
func2(11)
func2(22)
func2(undefined)

// CHECK: 1
// CHECK: 2
// CHECK: default
// CHECK: 1
// CHECK: 11
// CHECK: 22
// CHECK: 11
// CHECK: 22
// CHECK: 11
// CHECKERR_RE: ^$
// STATUS: 0