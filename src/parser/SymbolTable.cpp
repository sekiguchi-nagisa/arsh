/*
 * SymbolTable.cpp
 *
 *  Created on: 2015/01/10
 *      Author: skgchxngsxyz-osx
 */

#include "SymbolTable.h"

// #########################
// ##     SymbolEntry     ##
// #########################

SymbolEntry::SymbolEntry() {
}

SymbolEntry::~SymbolEntry() {
}


// ###############################
// ##     CommonSymbolEntry     ##
// ###############################

CommonSymbolEntry::CommonSymbolEntry(DSType *type, bool readOnly, bool global):
		SymbolEntry(), flag(0), type(type) {
	if(readOnly) {
		this->flag |= CommonSymbolEntry::READ_ONLY;
	}
	if(global) {
		this->flag |= CommonSymbolEntry::GLOBAL;
	}
}

CommonSymbolEntry::~CommonSymbolEntry() {
}

DSType *CommonSymbolEntry::getType() {
	 return this->type;
}

bool CommonSymbolEntry::isReadOnly() {
	return (this->flag & CommonSymbolEntry::READ_ONLY) == CommonSymbolEntry::READ_ONLY;
}

bool CommonSymbolEntry::isGlobal() {
	return (this->flag & CommonSymbolEntry::GLOBAL) == CommonSymbolEntry::GLOBAL;
}


// #############################
// ##     FuncSymbolEntry     ##
// #############################

FuncSymbolEntry::FuncSymbolEntry(FunctionHandle *handle):
		SymbolEntry(), handle(handle) {
}

DSType *FuncSymbolEntry::getType() {
	return this->handle->getFuncType();
}

bool FuncSymbolEntry::isReadOnly() {
	return true;
}

bool FuncSymbolEntry::isGlobal() {
	return true;
}

FunctionHandle *FuncSymbolEntry::getHandle() {
	return this->handle;
}


// #########################
// ##     SymbolTable     ##
// #########################

SymbolTable::SymbolTable() {
	// TODO Auto-generated constructor stub

}

SymbolTable::~SymbolTable() {
	// TODO Auto-generated destructor stub
}


