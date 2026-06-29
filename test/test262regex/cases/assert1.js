// RUN: exec $cmd -n $self

assert(true);

assert("hey");

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: Expected true but got hey
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/assert1\.js:5

// STATUS: 1