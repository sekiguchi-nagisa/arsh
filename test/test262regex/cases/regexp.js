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
assert.sameValue(''.match('..'), null);
assert.compareArray(/(.)(.)/.exec('あい'), ['あい', 'あ', 'い']);
assert.compareArray("あい".match(/(.)(.)/), ['あい', 'あ', 'い']);
assert.compareArray("あい".match(), ['']);
assert.compareArray("あい".match(/(.)/), ['あ', 'あ']);
assert.compareArray("あい".match(/(.)/g), ['あ', 'い']);
console.log("あい".match(/(.)/));
console.log("あい".match(/(.)/g));
console.log("😄".match(/(?:)/g));
console.log("😄".match(/(?:)/gu));

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


// CHECK: [ あ, あ, groups: undefined, index: 0, input: あい ]
// CHECK: [ あ, い ]
// CHECK: [ , ,  ]
// CHECK: [ ,  ]
// CHECKERR_RE: ^$
// STATUS: 0