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

// constructor
assert.sameValue(0, Number());
assert.sameValue(0, Number(null));
assert.sameValue(NaN, Number(undefined));
assert.sameValue(0, Number(false));
assert.sameValue(1, Number(true));
assert.sameValue(0, Number(''));
assert.sameValue(NaN, Number('fajrei'));
assert.sameValue(123, Number('123'));
assert.sameValue(-0.0, Number('-0.0'));
assert.sameValue(3.14, Number('3.14'));
assert.sameValue(NaN, Number('inf'));
assert.sameValue(NaN, Number('-inf'));
assert.sameValue(Infinity, Number('Infinity'));
assert.sameValue(Infinity, Number('+Infinity'));
assert.sameValue(-Infinity, Number('-Infinity'));
assert.sameValue(Infinity, Number(Infinity));
assert.sameValue(3.14, Number(3.14));

// toString
assert.sameValue('1100100', (100).toString(2));
assert.sameValue('10201', (100).toString(3));
assert.sameValue('-1210', (-100).toString(4));
assert.sameValue('400', (100).toString(5));
assert.sameValue('244', (100).toString(6));
assert.sameValue('202', (100).toString(7));
assert.sameValue('144', (100).toString(8));
assert.sameValue('121', (100).toString(9));
assert.sameValue('100', (100).toString(10));
assert.sameValue('91', (100).toString(11));
assert.sameValue('84', (100).toString(12));
assert.sameValue('79', (100).toString(13));
assert.sameValue('72', (100).toString(14));
assert.sameValue('6a', (100).toString(15));
assert.sameValue('64', (100).toString(16));
assert.sameValue('5f', (100).toString(17));
assert.sameValue('5a', (100).toString(18));
assert.sameValue('55', (100).toString(19));
assert.sameValue('50', (100).toString(20));
assert.sameValue('4g', (100).toString(21));
assert.sameValue('-4c', (-100).toString(22));
assert.sameValue('48', (100).toString(23));
assert.sameValue('44', (100).toString(24));
assert.sameValue('40', (100).toString(25));
assert.sameValue('3m', (100).toString(26));
assert.sameValue('3j', (100).toString(27));
assert.sameValue('3g', (100).toString(28));
assert.sameValue('3d', (100).toString(29));
assert.sameValue('3a', (100).toString(30));
assert.sameValue('37', (100).toString(31));
assert.sameValue('34', (100).toString(32));
assert.sameValue('31', (100).toString(33));
assert.sameValue('2w', (100).toString(34));
assert.sameValue('2u', (100).toString(35));
assert.sameValue('2s', (100).toString(36));


// toString

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0