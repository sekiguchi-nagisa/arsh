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

let cond1 = function (b) {
    if (b) {
        return "OK1";
    } else {
        return "NG1";
    }
};

let cond2 = function (b) {
    if (b < 0) {
        return "Negative";
    } else if (b > 0) {
        return "Positive";
    }
};

let cond3 = function (b) {
    if (b < 0)
        if (b > -100)
            console.log("larger than -100");
        else
            console.log("Negative");
};

assert.sameValue("OK1", cond1(true));
assert.sameValue("NG1", cond1(false));
assert.sameValue("Negative", cond2(-3.13));
assert.sameValue("Positive", cond2(3.13));
assert.sameValue(undefined, cond2(-0.0));

cond3(1000);    // no print
cond3(-12);
cond3(-1000);

// CHECK: larger than -100
// CHECK: Negative
// CHECKERR_RE: ^$
// STATUS: 0