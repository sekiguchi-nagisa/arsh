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

try {
    throw "failed";
} catch (e) {
    console.log(e);
}

var func = function (pattern) {
    try {
        new RegExp(pattern);
        return true;
    } catch {
        return false;
    }
};
assert(func('1234'));
assert(!func('***'));

var func2 = function () {
    try {
        return 1;
    } finally {
        return 2;
    }
};
assert.sameValue(2, func2());

try {
    try {
        throw new Error("error1");
    } finally {
        console.log("finally1");
    }
} catch (ex) {
    console.log('outer1', ex.message);
}

try {
    try {
        throw new Error('error2');
    } catch (ex) {
        console.log('inner2', ex.message);
    } finally {
        console.log('finally2');
    }
} catch (ex) {
    console.log('outer2', ex.message);
}

try {
    try {
        throw new Error('error3');
    } catch (ex) {
        console.log('inner3', ex.message);
        throw ex;
    } finally {
        console.log('finally3');
    }
} catch (ex) {
    console.log('outer3', ex.message);
}

// CHECK: failed

// CHECK: finally1
// CHECK: outer1 error1

// CHECK: inner2 error2
// CHECK: finally2

// CHECK: inner3 error3
// CHECK: finally3
// CHECK: outer3 error3

// CHECKERR_RE: ^$

// STATUS: 0