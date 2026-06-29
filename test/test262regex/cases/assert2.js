// RUN: exec $cmd -n $self

assert(true);

assert(false, "failed");

//    CHECKERR: [uncaught]
//    CHECKERR: Test262Error: failed
// CHECKERR_RE:     at .+\/test\/test262regex\/cases\/assert2\.js:5

// STATUS: 1