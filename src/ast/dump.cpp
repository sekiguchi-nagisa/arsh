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
#include <cxxabi.h>

#include "dump.h"
#include "../ast/Node.h"
#include "../core/TypePool.h"
#include "../misc/debug.h"

#define OUT (this->stream)

namespace ydsh {
namespace ast {

Writer::Writer(std::ostream &stream, TypePool &pool) :
        stream(stream), pool(pool), indentLevel(0) {
}

void Writer::write(const char *fieldName, const char *value) {
    std::string str(value);
    this->write(fieldName, str);
}

void Writer::write(const char *fieldName, const std::string &value) {
    this->writeName(fieldName);
    OUT << value << std::endl;
}

void Writer::write(const char *fieldName, const std::vector<Node *> &nodes) {
    this->writeName(fieldName);
    OUT << std::endl;

    this->enterIndent();
    for(Node *node : nodes) {
        this->write(*node);
    }
    this->exitIndent();
}

void Writer::write(const char *fieldName, const std::list<Node *> &nodes) {
    this->writeName(fieldName);
    OUT << std::endl;

    this->enterIndent();
    for(Node *node : nodes) {
        this->write(*node);
    }
    this->exitIndent();
}

void Writer::write(const char *fieldName, const Node &node) {
    // write field name
    this->writeName(fieldName);

    // write node body
    OUT << std::endl;
    this->enterIndent();
    this->write(node);
    this->exitIndent();
}

void Writer::write(const char *fieldName, const TypeToken &tok) {
    this->writeName(fieldName);
    OUT << tok.toTokenText() << std::endl;
}

void Writer::write(const char *fieldName, const DSType &type) {
    this->writeName(fieldName);
    OUT << this->pool.getTypeName(type) << std::endl;
}

void Writer::write(const char *fieldName, const std::vector<TypeToken *> &toks) {
    this->writeName(fieldName);
    unsigned int size = toks.size();
    for(unsigned int i = 0; i < size; i++) {
        if(i > 0) {
            OUT << ", ";
        }
        TypeToken *tok = toks[i];
        OUT << (tok == 0 ? "(null)" : tok->toTokenText());
    }
    OUT << std::endl;
}

void Writer::writeNull(const char *fieldName) {
    this->writeName(fieldName);
    OUT << "(null)" << std::endl;
}

void Writer::write(const Node &node) {
    this->writeNodeHeader(node);
    node.dump(*this);
}

void Writer::enterIndent() {
    this->indentLevel++;
}

void Writer::exitIndent() {
    this->indentLevel--;
}

void Writer::writeIndent() {
    for(unsigned int i = 0; i < this->indentLevel; i++) {
        OUT << "  ";
    }
}

void Writer::writeNodeHeader(const Node &node) {
    const std::type_info &info = typeid(node);
    int status;

    char *className = abi::__cxa_demangle(info.name(), 0, 0, &status);
    if(className == 0 || status != 0) {
        fatal("demangle typeinfo failed: %s\n", info.name());
    }
    DSType *type = node.getType();

    this->writeIndent();
    OUT << "@" << className << " (lineNum: " << node.getLineNum()
    << ", type: " << (type != 0 ? this->pool.getTypeName(*type) : "(null)")
    << ")" << std::endl;

    // free demangled name
    free(className);
}

void Writer::writeName(const char *fieldName) {
    this->writeIndent();
    OUT << " " << fieldName << ": ";
}

void dumpAST(std::ostream &out, TypePool &pool, const RootNode &rootNode) {
    Writer writer(out, pool);
    rootNode.dump(writer);
}

} // namespace ast
} // namespace ydsh