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

// string
let ss = "あい3\uD87E\uDC04";
assert.sameValue(undefined, ss[-1]);
assert.sameValue(undefined, ss['0.0']);
assert.sameValue(undefined, ss['1.0']);
assert.sameValue(undefined, ss[false]);
assert.sameValue(undefined, ss[true]);
assert.sameValue(undefined, ss['hey']);
assert.sameValue(undefined, ""[0]);
assert.sameValue(undefined, ss[5]);
assert.sameValue(undefined, ss[100]);
assert.sameValue('あ', ss['0']);
assert.sameValue('あ', ss[-0.0]);
assert.sameValue('あ', ss[0.0]);
assert.sameValue('あ', ss[0]);
assert.sameValue('い', ss[1.000]);
assert.sameValue('い', ss[1]);
assert.sameValue('い', ss['1']);
assert.sameValue('3', ss[2]);
assert.sameValue('\uD87E', ss[3]);
assert.sameValue('\uDC04', ss[4]);
assert.sameValue(5, ss['length']);

// array
let aa = [1, 2, 3];
assert.sameValue(undefined, aa[-1]);
assert.sameValue(undefined, aa[4]);
assert.sameValue(undefined, aa[100]);
assert.sameValue(undefined, [][0]);
assert.sameValue(undefined, [][3]);
assert.sameValue(1, aa[0]);
assert.sameValue(2, aa[1]);
assert.sameValue(3, aa[2]);
assert.sameValue(3, aa['length']);

// regex
let ret = /1(?<A>23)4/.exec("<1234>");
assert.sameValue(2, ret['length']);
assert.sameValue('1234', ret[0]);
assert.sameValue('23', ret[1]);
assert.sameValue(1, ret['index']);
assert.sameValue('<1234>', ret['input']);
assert.sameValue('23', ret['groups']['A']);

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0