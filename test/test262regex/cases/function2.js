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

function (n) {
    return 1234 + n;
}


// CHECK_RE: ^$
//    CHECK_ERR: function (n) {
//    CHECK_ERR: ^~~~~~~~
//    CHECK_ERR: [uncaught]
//    CHECK_ERR: SyntaxError: function declaration requires a name
// CHECK_ERR_RE: at .+/test/test262regex/cases/function2\.js:14

// STATUS: 1