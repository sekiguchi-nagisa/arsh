/*
 * SymbolTable.h
 *
 *  Created on: 2015/01/10
 *      Author: skgchxngsxyz-osx
 */

#ifndef PARSER_SYMBOLTABLE_H_
#define PARSER_SYMBOLTABLE_H_

#include "../core/DSType.h"
#include "../core/CalleeHandle.h"

class SymbolEntry {
public:
    SymbolEntry();
    virtual ~SymbolEntry();

    /**
     * return the type of symbol(variable, function)
     */
    virtual DSType *getType() = 0;

    /**
     * return true, if read only entry
     */
    virtual bool isReadOnly() = 0;

    /**
     * return true, if global entry
     */
    virtual bool isGlobal() = 0;
};

class CommonSymbolEntry: public SymbolEntry {
private:
    const static int READ_ONLY = 1 << 0;
    const static int GLOBAL = 1 << 1;

    /**
     * 1 << 0: read only
     * 1 << 1: global
     */
    unsigned char flag;

    DSType *type;

public:
    CommonSymbolEntry(DSType *type, bool readOnly, bool global);
    ~CommonSymbolEntry();

    DSType *getType();	// override
    bool isReadOnly();	// override
    bool isGlobal();	// override
};

class FuncSymbolEntry: public SymbolEntry {
private:
    FunctionHandle *handle;

public:
    FuncSymbolEntry(FunctionHandle *handle);

    DSType *getType();	// override

    /**
     * return always true
     */
    bool isReadOnly();	// override

    /**
     * currently, return always true
     */
    bool isGlobal();	// override

    FunctionHandle *getHandle();
};

class SymbolTable {
public:
    SymbolTable();
    virtual ~SymbolTable();
};

#endif /* PARSER_SYMBOLTABLE_H_ */
