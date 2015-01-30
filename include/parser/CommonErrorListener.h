/*
 * CommonErrorListener.h
 *
 *  Created on: 2015/01/30
 *      Author: skgchxngsxyz-osx
 */

#ifndef YDSH_INCLUDE_PARSER_COMMONERRORLISTENER_H_
#define YDSH_INCLUDE_PARSER_COMMONERRORLISTENER_H_

#include <parser/ErrorListener.h>

class CommonErrorListener : public ErrorListener {
public:
    CommonErrorListener();
    ~CommonErrorListener();

    void displayTypeError(const std::string &sourceName,
            const TypeCheckException &e); // override
};

#endif /* YDSH_INCLUDE_PARSER_COMMONERRORLISTENER_H_ */
