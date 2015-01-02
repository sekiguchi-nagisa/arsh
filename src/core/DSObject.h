/*
 * DSObject.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef CORE_DSOBJECT_H_
#define CORE_DSOBJECT_H_

class DSObject {
protected:
	/**
	 * type of this object
	 */
	DSType *type;
	int fieldSize;

	/**
	 * contains field(include function object)
	 */
	DSObject** fieldTable;

public:
	DSObject(DSType *type);
	virtual ~DSObject();

	DSType *getType();
	int getFieldSize();
	DSObject **getFieldTable();
};


class Int64_Object : public DSObject {
private:
	long value;

public:
	Int64_Object(DSType *type, long value);

	long getValue();
};


class Float_Object : public DSObject {
private:
	double value;

public:
	Float_Object(DSType *type, double value);

	double getValue();
};


class Boolean_Object : public DSObject {
private:
	bool value;

public:
	Boolean_Object(DSType *type, bool value);

	bool getValue();
};


class String_Object : public DSObject {
private:
	std::string value;

public:
	String_Object(DSType *type, std::string value);

	std::string getValue();
};


#endif /* CORE_DSOBJECT_H_ */
