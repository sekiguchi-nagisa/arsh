/*
 * Node.h
 *
 *  Created on: 2014/12/31
 *      Author: skgchxngsxyz-osx
 */

#ifndef PARSER_NODE_H_
#define PARSER_NODE_H_

class Node {
protected:
	int lineNum;

public:
	Node(int lineNum);
	virtual ~Node();

	int getLineNum();
	void accept(NodeVisitor *visitor) = 0;
};


class ExprNode {

};

#endif /* PARSER_NODE_H_ */
