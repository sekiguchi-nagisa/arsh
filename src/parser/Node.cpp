/*
 * Node.cpp
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#include "Node.h"

/**
 * Node implementation
 */
Node::Node(int lineNum) : lineNum(lineNum) {
}

Node::~Node() {
	// TODO Auto-generated destructor stub
}

int Node::getLineNum() {
	return this->lineNum;
}

/**
 * ExprNode implementation
 */
