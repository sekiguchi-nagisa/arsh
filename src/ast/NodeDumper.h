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

#ifndef YDSH_NODEDUMPER_H
#define YDSH_NODEDUMPER_H

#define NAME(f) #f

#include <string>
#include <ostream>
#include <vector>
#include <list>

namespace ydsh {
namespace core {

class TypePool;
class DSType;

}
};

namespace ydsh {
namespace ast {

using namespace ydsh::core;

class Node;
class TypeToken;
class RootNode;

class NodeDumper {
private:
    std::ostream &stream;
    TypePool &pool;

    unsigned int indentLevel;

public:
    NodeDumper(std::ostream &stream, core::TypePool &pool) :
            stream(stream), pool(pool), indentLevel(0) { }

    ~NodeDumper() = default;

    /**
     * dump field
     */
    void dump(const char *fieldName, const char *value);

    void dump(const char *fieldName, const std::string &value);

    void dump(const char *fieldName, const std::vector<Node *> &nodes);

    void dump(const char *fieldName, const std::list<Node *> &nodes);

    /**
     * dump node with indent
     */
    void dump(const char *fieldName, const Node &node);

    void dump(const char *fieldName, const TypeToken &tok);

    void dump(const char *fieldName, const DSType &type);

    void dump(const char *fieldName, const std::vector<TypeToken *> &toks);

    void dumpNull(const char *fieldName);

    /**
     * dump node without indent
     */
    void dump(const Node &node);

    /**
     * entry point
     */
    void operator()(const RootNode &rootNode);

    static void dump(std::ostream &out, TypePool &pool, const RootNode &rootNode);

private:
    void enterIndent();

    void exitIndent();

    void writeIndent();

    void writeNodeHeader(const Node &node);

    void writeName(const char *fieldName);
};

} // namespace ast
} // namespace ydsh

// helper macro definition
#define DUMP(field)  dumper.dump(NAME(field), field)
#define DUMP_PRIM(field) dumper.dump(NAME(field), std::to_string(field))
#define DUMP_PTR(field) \
    do {\
        if(field == nullptr) {\
            dumper.dumpNull(NAME(field));\
        } else {\
            dumper.dump(NAME(field), *field);\
        }\
    } while(false)

// not directly use it.
#define GEN_ENUM_STR(ENUM, out) case ENUM: out = #ENUM; break;

#define DECODE_ENUM(out, val, EACH_ENUM) \
    do {\
        switch(val) {\
        EACH_ENUM(GEN_ENUM_STR, out)\
        }\
    } while(false)

// not directly use it.
#define GEN_FLAG_STR(FLAG, out, set) \
        if(((set) & FLAG) == FLAG) { if(c++ > 0) { out += " | "; } out += #FLAG; }

#define DECODE_BITSET(out, set, EACH_FLAG) \
    do {\
        unsigned int c = 0;\
        EACH_FLAG(GEN_FLAG_STR, out, set) \
    } while(false)


#endif //YDSH_NODEDUMPER_H
