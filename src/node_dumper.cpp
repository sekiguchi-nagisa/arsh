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

#include "node_dumper.h"
#include "node.h"

namespace ydsh {

void NodeDumper::dump(const char *fieldName, const char *value) {
    this->writeName(fieldName);

    fputc('"', this->fp);
    while(*value) {
        char ch = *(value++);
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
            fputc('\\', this->fp);
        }
        fputc(ch, this->fp);
    }
    fputs("\"\n", this->fp);
}

void NodeDumper::dump(const char *fieldName, const std::list<Node *> &nodes) {
    this->writeName(fieldName);
    this->newline();

    this->enterIndent();
    for(Node *node : nodes) {
        this->indent();
        fputs("- ", this->fp);
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
    this->newline();
    this->enterIndent();
    this->dump(node);
    this->leaveIndent();
}

void NodeDumper::dump(const char *fieldName, const DSType &type) {
    this->dump(fieldName, this->pool.getTypeName(type));
}

void NodeDumper::dump(const char *fieldName, TokenKind kind) {
    this->dump(fieldName, toString(kind));
}

void NodeDumper::dumpNull(const char *fieldName) {
    this->writeName(fieldName);
    this->newline();
}

void NodeDumper::dump(const Node &node) {
    this->indent();
    this->dumpNodeHeader(node);
    node.dump(*this);
    fflush(this->fp);
}

void NodeDumper::indent() {
    for(unsigned int i = 0; i < this->indentLevel; i++) {
        fputs("  ", this->fp);
    }
}

static const char *toString(NodeKind kind) {
    const char *table[] = {
#define GEN_STR(E) #E,
        EACH_NODE_KIND(GEN_STR)
#undef GEN_STR
    };
    return table[static_cast<unsigned char>(kind)];
}

void NodeDumper::dumpNodeHeader(const Node &node, bool inArray) {
    fprintf(this->fp, "nodeKind: %s\n", toString(node.getNodeKind()));

    if(inArray) {
        this->enterIndent();
    }

    this->indent(); fprintf(this->fp, "token: \n");
    this->enterIndent();
    this->indent(); fprintf(this->fp, "pos: %d\n", node.getPos());
    this->indent(); fprintf(this->fp, "size: %d\n", node.getSize());
    this->leaveIndent();
    this->indent(); fprintf(this->fp, "type: %s\n",
                            (!node.isUntyped() ? this->pool.getTypeName(node.getType()).c_str() : ""));

    if(inArray) {
        this->leaveIndent();
    }
}

void NodeDumper::dumpNodes(const char *fieldName, Node * const * begin, Node *const * end) {
    this->writeName(fieldName);
    this->newline();

    this->enterIndent();
    for(; begin != end; ++begin) {
        Node *node = *begin;

        this->indent();
        fputs("- ", this->fp);
        this->dumpNodeHeader(*node, true);
        this->enterIndent();
        node->dump(*this);
        this->leaveIndent();
    }
    this->leaveIndent();
}

void NodeDumper::writeName(const char *fieldName) {
    this->indent(); fprintf(this->fp, "%s: ", fieldName);
}

void NodeDumper::operator()(const RootNode &rootNode) {
    this->dump(rootNode);
}

void NodeDumper::dump(FILE *fp, TypePool &pool, const RootNode &rootNode) {
    NodeDumper writer(fp, pool);
    writer(rootNode);
}

} // namespace ydsh