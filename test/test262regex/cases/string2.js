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
assert.sameValue("—", String.fromCharCode(0x12014));
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

// charAt
assert.sameValue('', 'he'.charAt(-1));
assert.sameValue('', 'he'.charAt(2));
assert.sameValue('', 'he'.charAt(100));
assert.sameValue('', 'he'.charAt(-1100));
assert.sameValue('1', '1\nåあ'.charAt());
assert.sameValue('1', '1\nåあ'.charAt(0));
assert.sameValue('\n', '1\nåあ'.charAt(1));
assert.sameValue('å', '1\nåあ'.charAt(2));
assert.sameValue('あ', '1\nåあ'.charAt(3));
assert.sameValue('\ud84f', "𣴀".charAt(0));
assert.sameValue('\udd00', "𣴀".charAt(1));

// charCodeAt
assert.sameValue(NaN, 'he'.charCodeAt(-1));
assert.sameValue(NaN, 'he'.charCodeAt(2));
assert.sameValue(NaN, 'he'.charCodeAt(100));
assert.sameValue(NaN, 'he'.charCodeAt(-1100));
assert.sameValue(0xd84f, "𣴀1\nåあ".charCodeAt());
assert.sameValue(0xd84f, "𣴀1\nåあ".charCodeAt(0));
assert.sameValue(0xdd00, "𣴀1\nåあ".charCodeAt(1));
assert.sameValue(49, "𣴀1\nåあ".charCodeAt(2));
assert.sameValue(10, "𣴀1\nåあ".charCodeAt(3));
assert.sameValue(229, "𣴀1\nåあ".charCodeAt(4));
assert.sameValue(12354, "𣴀1\nåあ".charCodeAt(5));
assert.sameValue(NaN, "𣴀1\nåあ".charCodeAt(6));

// codePointAt
assert.sameValue(undefined, 'he'.codePointAt(-1));
assert.sameValue(undefined, 'he'.codePointAt(2));
assert.sameValue(undefined, 'he'.codePointAt(100));
assert.sameValue(undefined, 'he'.codePointAt(-1100));
assert.sameValue(146688, "𣴀1\nåあ".codePointAt());
assert.sameValue(146688, "𣴀1\nåあ".codePointAt(0));
assert.sameValue(0xdd00, "𣴀1\nåあ".codePointAt(1));
assert.sameValue(49, "𣴀1\nåあ".codePointAt(2));
assert.sameValue(10, "𣴀1\nåあ".codePointAt(3));
assert.sameValue(229, "𣴀1\nåあ".codePointAt(4));
assert.sameValue(12354, "𣴀1\nåあ".codePointAt(5));
assert.sameValue(undefined, "𣴀1\nåあ".codePointAt(6));


// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0