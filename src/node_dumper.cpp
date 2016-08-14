/*
 * Copyright (C) 2015-2016 Nagisa Sekiguchi
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

#include "node_dumper.h"
#include "node.h"
#include "misc/demangle.hpp"

namespace ydsh {

void NodeDumper::dump(const char *fieldName, const char *value) {
    std::string str(value);
    this->dump(fieldName, str);
}

void NodeDumper::dump(const char *fieldName, const std::string &value) {
    this->writeName(fieldName);

    this->stream << '"';
    for(char ch : value) {
        bool escape = true;
        switch(ch) {
        case '\t':
            ch = 't';
            break;
        case '\r':
            ch = 'r';
            break;
        case '\n':
            ch = 'n';
            break;
        case '"':
            ch = '"';
            break;
        case '\\':
            ch = '\\';
            break;
        default:
            escape = false;
            break;
        }
        if(escape) {
            this->stream << '\\';
        }
        this->stream << ch;
    }
    this->stream << '"' << std::endl;
}

void NodeDumper::dump(const char *fieldName, const std::vector<Node *> &nodes) {
    this->writeName(fieldName);
    this->stream << std::endl;

    this->enterIndent();
    for(Node *node : nodes) {
        this->indent();
        this->stream << "- ";
        this->dumpNodeHeader(*node, true);
        this->enterIndent();
        node->dump(*this);
        this->leaveIndent();
    }
    this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const std::list<Node *> &nodes) {
    this->writeName(fieldName);
    this->stream << std::endl;

    this->enterIndent();
    for(Node *node : nodes) {
        this->indent();
        this->stream << "- ";
        this->dumpNodeHeader(*node, true);
        this->enterIndent();
        node->dump(*this);
        this->leaveIndent();
    }
    this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const Node &node) {
    // write field name
    this->writeName(fieldName);

    // write node body
    this->stream << std::endl;
    this->enterIndent();
    this->dump(node);
    this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const DSType &type) {
    this->writeName(fieldName);
    this->stream << this->pool.getTypeName(type) << std::endl;
}

void NodeDumper::dumpNull(const char *fieldName) {
    this->writeName(fieldName);
    this->stream << std::endl;
}

void NodeDumper::dump(const Node &node) {
    this->indent();
    this->dumpNodeHeader(node);
    node.dump(*this);
}

void NodeDumper::indent() {
    for(unsigned int i = 0; i < this->indentLevel; i++) {
        this->stream << "  ";
    }
}

void NodeDumper::dumpNodeHeader(const Node &node, bool inArray) {
    std::string className = Demangle()(typeid(node));

    this->stream << "__Node: " << std::endl;
    this->enterIndent();
    if(inArray) {
        this->enterIndent();
    }

    this->indent(); this->stream << "__kind: " << strrchr(className.c_str(), ':') + 1 << std::endl;
    this->indent(); this->stream << "pos: " << node.getStartPos() << std::endl;
    this->indent(); this->stream << "size: " << node.getSize() << std::endl;
    this->indent(); this->stream << "type: " <<
            (!node.isUntyped() ? this->pool.getTypeName(node.getType()) : "") << std::endl;

    this->leaveIndent();
    if(inArray) {
        this->leaveIndent();
    }
}

void NodeDumper::writeName(const char *fieldName) {
    this->indent(); this->stream << fieldName << ": ";
}

void NodeDumper::operator()(const RootNode &rootNode) {
    this->dump(rootNode);
}

void NodeDumper::dump(std::ostream &out, TypePool &pool, const RootNode &rootNode) {
    NodeDumper writer(out, pool);
    writer(rootNode);
}

} // namespace ydsh