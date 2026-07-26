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

// fromCharCode
assert.sameValue("", String.fromCharCode());
assert.sameValue("ABC", String.fromCharCode(65, 66, 67));
assert.sameValue("—", String.fromCharCode(0x2014));
assert.sameValue("\u{1F303}", String.fromCharCode(0xd83c, 0xdf03));
assert.sameValue("\uD83C\uDF03", String.fromCharCode(55356, 57091));

// fromCodePoint
assert.sameValue("", String.fromCodePoint());
assert.sameValue("*", String.fromCodePoint(42));
assert.sameValue("AZ", String.fromCodePoint(65, 90));
assert.sameValue("\u0404", String.fromCodePoint(0x404));
assert.sameValue("\uD87E\uDC04", String.fromCodePoint(0x2f804));
assert.sameValue("\uD87E\uDC04", String.fromCodePoint(194564));
assert.sameValue("\uD834\uDF06a\uD834\uDF07", String.fromCodePoint(0x1d306, 0x61, 0x1d307));
assert.throws(RangeError, function () {
    String.fromCodePoint("_");
});
assert.throws(RangeError, function () {
    String.fromCodePoint(Infinity);
});
assert.throws(RangeError, function () {
    String.fromCodePoint(-1);
});
assert.throws(RangeError, function () {
    String.fromCodePoint(3.14);
});
assert.throws(RangeError, function () {
    String.fromCodePoint(3e-2);
});
assert.throws(RangeError, function () {
    String.fromCodePoint(NaN);
});
assert.throws(RangeError, function () {
    String.fromCodePoint(99999999);
});


// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0