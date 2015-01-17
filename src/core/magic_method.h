/*
 * magic_method.h
 *
 *  Created on: 2015/01/17
 *      Author: skgchxngsxyz-opensuse
 */

#ifndef SRC_CORE_MAGIC_METHOD_H_
#define SRC_CORE_MAGIC_METHOD_H_

#include <string>

// unary op definition
extern const std::string PLUS;  // +
extern const std::string MINUS; // -
extern const std::string NOT;   // not

// binary op definition
extern const std::string ADD;   // +
extern const std::string SUB;   // -
extern const std::string MUL;   // *
extern const std::string DIV;   // /
extern const std::string MOD;   // %

extern const std::string EQ;    // ==
extern const std::string NE;    // !=

extern const std::string LT;    // <
extern const std::string GT;    // >
extern const std::string LE;    // <=
extern const std::string GE;    // >=

extern const std::string AND;   // &
extern const std::string OR;    // |
extern const std::string XOR;   // ^

extern const std::string RE_EQ; // =~
extern const std::string RE_NE; // !~

// indexer op
extern const std::string GET;   // []
extern const std::string SET;   // [] =

// iterator
extern const std::string RESET; // must be Func<Void> type
extern const std::string NEXT;  // must be Func<Any> type
extern const std::string HAS_NEXT;  // must be Func<Boolean> type


#endif /* SRC_CORE_MAGIC_METHOD_H_ */
