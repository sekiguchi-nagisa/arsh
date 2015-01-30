/*
 * ErrorListener.h
 *
 *  Created on: 2015/01/30
 *      Author: skgchxngsxyz-osx
 */

#ifndef YDSH_INCLUDE_PARSER_ERRORLISTENER_H_
#define YDSH_INCLUDE_PARSER_ERRORLISTENER_H_

#include <parser/TypeCheckError.h>

class ErrorListener {
public:
    ErrorListener();
    virtual ~ErrorListener();

    virtual void displyTypeError(const std::string &sourceName,
            const TypeCheckException &e) = 0;
};

#endif /* YDSH_INCLUDE_PARSER_ERRORLISTENER_H_ */
