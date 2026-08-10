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

// variable
let vv = "hey";
vv = 12;
assert.sameValue(12, vv);
vv += 34;
assert.sameValue(46, vv);
vv += false;
assert.sameValue(46, vv);
vv += true;
assert.sameValue(47, vv);
vv += '3';
assert.sameValue('473', vv);
vv += 'hey';
assert.sameValue('473hey', vv);
vv -= 12;
assert.sameValue(NaN, vv);
vv = '23';
vv -= 10;
assert.sameValue(13, vv);
assert.sameValue(13, vv++);
assert.sameValue(14, vv);
assert.sameValue(15, ++vv);
assert.sameValue(15, vv);
assert.sameValue(15, vv--);
assert.sameValue(14, vv);
assert.sameValue(13, --vv);
assert.sameValue(13, vv);

// array
let aa = ["hey"];
aa[0] = null;
assert.sameValue(null, aa[0]);
aa[5] = 100;
assert.sameValue(undefined, aa[1]);
assert.sameValue(undefined, aa[2]);
assert.sameValue(undefined, aa[3]);
assert.sameValue(undefined, aa[4]);
assert.sameValue(100, aa[5]);
aa[5] -= 'hey';
assert.sameValue(NaN, aa[5]);
aa[4] += '!';
assert.sameValue('undefined!', aa[4]);

// field
let oo = {AAA: 12};
assert.sameValue(12, oo.AAA);
oo.AAA += 3.14;
assert.sameValue(15.14, oo.AAA);
oo.BBB = false;
assert.sameValue(false, oo.BBB);

// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0