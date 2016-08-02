//
// Created by skgchxngsxyz-carbon on 16/07/31.
//

#include <stdint.h>
#include <stddef.h>

#include <lexer.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    using namespace ydsh;

    LexerMode modes[] = {
            yycSTMT, yycEXPR, yycNAME, yycDSTRING, yycTYPE, yycCMD
    };

    for(auto mode : modes) {
        Lexer lexer("<dummy>", reinterpret_cast<const char *>(data), size);
        lexer.pushLexerMode(mode);

        TokenKind kind;

        do {
            Token token;
            kind = lexer.nextToken(token);
        } while(kind != EOS && kind != INVALID);
    }
    return 0;
}