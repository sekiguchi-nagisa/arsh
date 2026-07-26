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
assert.sameValue('null', String(null));
assert.sameValue('undefined', String(undefined));
assert.sameValue('true', String(true));
assert.sameValue('false', String(false));
assert.sameValue('', String([]));
assert.sameValue('', String([null]));
assert.sameValue(',,', String([null, undefined, [undefined]]));
assert.sameValue(',,,23', String([null, undefined, [undefined, 23]]));
assert.sameValue('/12/gim', String(/12/mgi));

// for length
assert.sameValue(0, "".length);
assert.sameValue(1, "\0".length);
assert.sameValue(1, "あ".length);
assert.sameValue(1, "朶".length);
assert.sameValue(2, "𤅕".length);
assert.sameValue(4, "1𤅕あ".length);

// for slice
const str1 = "The morning is upon us.";
assert.sameValue("he morn", str1.slice(1, 8));
assert.sameValue("morning is upon u", str1.slice(4, -2));
assert.sameValue("is upon us.", str1.slice(12));
assert.sameValue("", str1.slice(30));

assert.sameValue("us.", str1.slice(-3));
assert.sameValue("us", str1.slice(-3, -1));
assert.sameValue("The morning is upon us", str1.slice(0, -1));
assert.sameValue("morning is upon us", str1.slice(4, -1));

assert.sameValue("is u", str1.slice(-11, 16));
assert.sameValue(" is u", str1.slice(11, -7));
assert.sameValue("n us", str1.slice(-5, -1));

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0