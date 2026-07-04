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

assert(/./s.test('\n'));
assert.sameValue(/(.)(.)/.exec(''), null);
assert.compareArray(/(.)(.)/.exec('あい'), ['あい', 'あ', 'い']);

const pattern = /(.)(.)$/di;
assert(pattern.hasIndices);
assert(!pattern.unicode);
assert(!pattern.unicodeSets);
assert(!pattern.dotAll);
assert(!pattern.multiline);
assert(pattern.ignoreCase);
assert(!pattern.sticky);
assert(!pattern.global);
assert.sameValue(pattern.lastIndex, 0);

const ret = pattern.exec('12あい');
assert.sameValue(ret.input, '12あい');
assert.sameValue(ret.index, 2);
assert.compareArray(ret, ['あい', 'あ', 'い']);
assert.sameValue(ret.length, 3);


// CHECK_RE: ^$
// CHECKERR_RE: ^$
// STATUS: 0