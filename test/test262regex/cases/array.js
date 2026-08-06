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
assert.compareArray([1, 2, 'hey', null, undefined, false], [1, 2, 'hey', null, undefined, false]);
assert.compareArray([1, 2, 'hey', null, undefined, false], new Array(1, 2, 'hey', null, undefined, false));

// push/join
var aa = [];
assert.sameValue(0, aa.length);
assert.sameValue('', aa.join());
aa.push(undefined);
assert.sameValue(1, aa.length);
assert.compareArray([undefined], aa);
assert.sameValue('', aa.join());
aa.push(null);
assert.sameValue(2, aa.length);
assert.compareArray([undefined, null], aa);
assert.sameValue(',', aa.join());
assert.sameValue('@', aa.join('@'));
aa.push(NaN);
assert.sameValue(3, aa.length);
assert.compareArray([undefined, null, NaN], aa);
assert.sameValue(',,NaN', aa.join());
assert.sameValue('@@NaN', aa.join('@'));

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0