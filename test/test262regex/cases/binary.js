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

// +
assert.sameValue(100, 25 + 75);
assert.sameValue(79.3, 12 + 67.3);
assert.sameValue(2, true + true);
assert.sameValue(1, true + null);
assert.sameValue(NaN, false + undefined);
assert.sameValue('true!!', true + '!' + '!');
assert.sameValue('!NaN!', '!' + NaN + '!');

// -
assert.sameValue(-50, 25 - 75);
assert.sameValue(NaN, 25 - undefined);
assert.sameValue(NaN, NaN - NaN);
assert.sameValue(-50, '25' - '75');
assert.sameValue(-Infinity, -Infinity - Infinity);
assert.sameValue(NaN, Infinity - Infinity);

// <
assert(12 < 40.9);
assert(!(12 < NaN));
assert(!(NaN < NaN));
assert(!(0.0 < -0.0));
assert(!(-0.0 < 0.0));
assert('12' < 40.9);
assert('1' < '2');
assert('a' < 'y');

// <=
assert(12 <= 12.00);
assert(0.0 <= 40.9);
assert(0.0 <= -0.0);
assert(-0.0 <= 0.0);
assert(!(0.0 <= '2134q'));
assert(!(0.0 <= NaN));
assert(!(NaN <= '2134q'));
assert('a' <= 'a');

// >
assert(12 > 3);
assert(12 > '3.0');
assert(Infinity > -Infinity);
assert('Infinity' > '-3.0');
assert('Z' > 'U');

// >=
assert(0.0 >= -0.0);
assert('-0.0' >= 0.0);
assert('b' >= 'a');
assert('#' >= '#');


// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0