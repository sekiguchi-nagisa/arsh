/*
 * Copyright (C) 2015 Nagisa Sekiguchi
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>

#include "NodeDumper.h"
#include "../ast/Node.h"

#define OUT (this->stream)

namespace ydsh {
namespace ast {

void NodeDumper::dump(const char *fieldName, const char *value) {
    std::string str(value);
    this->dump(fieldName, str);
}

void NodeDumper::dump(const char *fieldName, const std::string &value) {
    this->writeName(fieldName);
    OUT << value << std::endl;
}

void NodeDumper::dump(const char *fieldName, const std::vector<Node *> &nodes) {
    this->writeName(fieldName);
    OUT << std::endl;

    this->enterIndent();
    for(Node *node : nodes) {
        this->writeIndent();
        OUT << "- ";
        this->writeNodeHeader(*node);
        this->enterIndent();
        node->dump(*this);
        this->exitIndent();
    }
    this->exitIndent();
}

void NodeDumper::dump(const char *fieldName, const std::list<Node *> &nodes) {
    this->writeName(fieldName);
    OUT << std::endl;

    this->enterIndent();
    for(Node *node : nodes) {
        this->writeIndent();
        OUT << "- ";
        this->writeNodeHeader(*node);
        this->enterIndent();
        node->dump(*this);
        this->exitIndent();
    }
    this->exitIndent();
}

void NodeDumper::dump(const char *fieldName, const Node &node) {
    // write field name
    this->writeName(fieldName);

    // write node body
    OUT << std::endl;
    this->enterIndent();
    this->dump(node);
    this->exitIndent();
}

void NodeDumper::dump(const char *fieldName, const TypeToken &tok) {
    this->writeName(fieldName);
    OUT << tok.toTokenText() << std::endl;
}

void NodeDumper::dump(const char *fieldName, const DSType &type) {
    this->writeName(fieldName);
    OUT << this->pool.getTypeName(type) << std::endl;
}

void NodeDumper::dump(const char *fieldName, const std::vector<TypeToken *> &toks) {
    this->writeName(fieldName);
    unsigned int size = toks.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            OUT << ", ";
        }
        TypeToken *tok = toks[i];
        OUT << (tok == nullptr ? "(null)" : tok->toTokenText());
    }
    OUT << std::endl;
}

void NodeDumper::dumpNull(const char *fieldName) {
    this->writeName(fieldName);
    OUT << "(null)" << std::endl;
}

void NodeDumper::dump(const Node &node) {
    this->writeIndent();
    this->writeNodeHeader(node);
    node.dump(*this);
}

void NodeDumper::enterIndent() {
    this->indentLevel++;
}

void NodeDumper::exitIndent() {
    this->indentLevel--;
}

void NodeDumper::writeIndent() {
    for(unsigned int i = 0; i < this->indentLevel; i++) {
        OUT << "  ";
    }
}

void NodeDumper::writeNodeHeader(const Node &node) {
    std::string className = misc::Demangle()(typeid(node));
    DSType *type = node.getType();

    OUT << "@Node: " << className << " (lineNum: " << node.getLineNum()
    << ", type: " << (type != nullptr ? this->pool.getTypeName(*type) : "(null)")
    << ")" << std::endl;
}

void NodeDumper::writeName(const char *fieldName) {
    this->writeIndent();
    OUT << fieldName << ": ";
}

void NodeDumper::operator()(const RootNode &rootNode) {
    this->dump(rootNode);
}

void NodeDumper::dump(std::ostream &out, TypePool &pool, const RootNode &rootNode) {
    NodeDumper writer(out, pool);
    writer(rootNode);
}

} // namespace ast
} // namespace ydsh