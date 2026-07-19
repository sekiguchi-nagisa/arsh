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

assert.sameValue(12, +12);
console.log(+12);
assert.sameValue(-12, ---12);
console.log(-12);
assert.sameValue(false, !true);
console.log(!true);
assert.sameValue(true, !false);
console.log(!false);

assert.sameValue(undefined, void 1234);
console.log(void (1234));

assert.sameValue('undefined', typeof void 0);
assert.sameValue('object', typeof null);
assert.sameValue('boolean', typeof false);
assert.sameValue('boolean', typeof true);
assert.sameValue('number', typeof 12);
assert.sameValue('number', typeof 12.34);
assert.sameValue('number', typeof -12.34);
assert.sameValue('string', typeof "");
assert.sameValue('string', typeof "1234");
assert.sameValue('function', typeof assert);
assert.sameValue('function', typeof assert.sameValue);
assert.sameValue('object', typeof []);
assert.sameValue('object', typeof [1234]);
assert.sameValue('object', typeof /2345/);
assert.sameValue('object', typeof {});
assert.sameValue('object', typeof {we: 'hey'});

// CHECK: 12
// CHECK: -12
// CHECK: false
// CHECK: true
// CHECK: undefined
// CHECKERR_RE: ^$
// STATUS: 0