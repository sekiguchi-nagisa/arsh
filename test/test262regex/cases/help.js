// RUN: exec $cmd -h

// CHECK_RE: usage: .+\/tools\/test262regex\/re262 \[-dnm\] \[test case path\]
//    CHECK: Option:
//    CHECK:   -d  enable debug print
//    CHECK:   -n  no meta-data check
//    CHECK:   -m  meta-data only (not evaluate)

// STATUS: 2