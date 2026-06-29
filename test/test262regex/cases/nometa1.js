// RUN: exec $cmd $self


const matchSymbols = buildString({
    loneCodePoints: [0x0040, 0x002A], // @, *
    ranges: [
        [0x3042, 0x3093] // あ, ん
    ]
});

testPropertyEscapes(
    /^[\u3042-\u3099@*]$/u,
    matchSymbols,
    "[\\u3042-\\u3099@*]"
);

//    CHECKERR: [meta-data error] cannot extract meta data section
// CHECKERR_RE:   at .+\/test\/test262regex\/cases\/nometa1\.js

// STATUS: 1