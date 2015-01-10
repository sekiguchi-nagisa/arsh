/*
 * TypePool.h
 *
 *  Created on: 2015/01/10
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_TYPEPOOL_H_
#define CORE_TYPEPOOL_H_

class DSType;

class TypePool {
public:
	TypePool();
	virtual ~TypePool();

	/**
	 * get any type (root class of ydsh class)
	 */
	DSType *getAnyType();

	/**
	 * get void type (pseudo class representing for void)
	 */
	DSType *getVoidType();

	/**
	 * equivalent to getInt64Type()
	 */
	DSType *getIntType();
	DSType *getInt64Type();
	DSType *getFloatType();
	DSType *getBooleanType();
	DSType *getStringType();

	// for reified type.
	DSType *getBaseArrayType();	//TODO: return type
	DSType *getBaseMapType();	// TODO: return type
	DSType *getBasePairType();	// TODO: return type
};

#endif /* CORE_TYPEPOOL_H_ */
