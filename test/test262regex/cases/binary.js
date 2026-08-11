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

// %
assert.sameValue(3, 13 % 5);
assert.sameValue(-3, -13 % 5);
assert.sameValue(3, 13 % -5);
assert.sameValue(-3, -13 % -5);
assert.sameValue(1.5, 5.5 % 2);
assert.sameValue(1.5, 5.5 % 2);
assert.sameValue(-0.0, -4 % 2);
assert.sameValue(NaN, NaN % 5);
assert.sameValue(NaN, Infinity % 5);
assert.sameValue(NaN, Infinity % 0);
assert.sameValue(NaN, 12 % 0);
assert.sameValue(NaN, Infinity % Infinity);
assert.sameValue(NaN, NaN % Infinity);
assert.sameValue(0, 0 % Infinity);
assert.sameValue(-0, -0.0 % Infinity);
assert.sameValue(3, 3 % Infinity);
assert.sameValue(0, 0 % Infinity);

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

// ===
assert(0.0 === -0.0);
assert('ABD' === 'ABD');
assert(true === true);
assert(undefined === undefined);
assert(null === null);
assert(!(NaN === NaN));

// !==
assert(0.000001 !== -0.0);
assert('123' !== 123);
assert(true !== false);
assert(undefined !== null);
assert(NaN !== NaN);

// &&
assert.sameValue(false, 'fcaer' && false);
assert.sameValue(false, false && 'hey');
assert.sameValue('', '' && false);
assert.sameValue(undefined, undefined && 'hello');
assert.sameValue(null, null && 'hello');
assert.sameValue('hey', 12 && 'hey');
assert.sameValue(-0.0, -0.0 && 'hey');

// ||
assert.sameValue(12, 12 || 'hey');
assert.sameValue('hey', 0 || 'hey');
assert.sameValue('hey', null || 'hey');
assert.sameValue(null, undefined || null);
assert.sameValue(null, NaN || null);
assert.sameValue(Infinity, Infinity || null);
assert.sameValue(-Infinity, -Infinity || false);

// instanceof
assert(!(12 instanceof Number));
assert(!('hey' instanceof String));
assert(/frea/ instanceof RegExp);
assert(SyntaxError('error') instanceof SyntaxError);
assert(SyntaxError('error') instanceof Error);

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0