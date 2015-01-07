/*
 * RuntimeContext.h
 *
 *  Created on: 2015/01/07
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_RUNTIMECONTEXT_H_
#define CORE_RUNTIMECONTEXT_H_

#include <vector>

#include "DSObject.h"

class RuntimeContext {
private:
	/**
	 * contains global variables(or function)
	 */
	std::vector<DSObject*> globalVarTable;

	/**
	 * if not null, thrown exception.
	 */
	DSObject *thrownObject;

public:
	RuntimeContext();
	virtual ~RuntimeContext();

	/**
	 * add new global variable or function
	 */
	void addGlobalVar(DSObject *obj);

	/**
	 * update exist global variable.
	 * this is not type-safe method
	 */
	void updateGlobalVar(int varIndex, DSObject *obj);

	/**
	 * this is not type-safe method
	 */
	DSObject *getGlobalVar(int index);

	int getGlobalVarSize();

	void setThrownObject(DSObject *obj);
	void clearThrownObject();

	/**
	 * return null, if not thrown
	 */
	DSObject *getThrownObject();
};

#endif /* CORE_RUNTIMECONTEXT_H_ */
