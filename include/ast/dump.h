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

#ifndef AST_DUMP_H_
#define AST_DUMP_H_

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

class Writer {
private:
    /**
     * not call destructor.
     */
    std::ostream *stream;

    /**
     * not call destrcutor.
     */
    TypePool *pool;

    unsigned int indentLevel;

public:
    Writer(std::ostream *stream, core::TypePool *pool);

    ~Writer();

    /**
     * write field
     */
    void write(const char *fieldName, const std::string &value);

    void write(const char *fieldName, const std::vector<Node *> &nodes);

    void write(const char *fieldName, const std::list<Node *> &nodes);

    /**
     * write node with indent
     */
    void write(const char *fieldName, const Node &node);

    void write(const char *fieldName, const TypeToken &tok);

    void write(const char *fieldName, const DSType &type);

    void write(const char *fieldName, const std::vector<TypeToken *> &toks);

    void writeNull(const char *fieldName);

    /**
     * write node without indent
     */
    void write(const Node &node);

private:
    void enterIndent();

    void exitIndent();

    void writeIndent();

    void writeNodeHeader(const Node &node);

    void writeName(const char *fieldName);

    const static char *INDENT;
};

// entry point
void dumpAST(std::ostream &out, TypePool &pool, const RootNode &rootNode);

} // namespace ast
} // namespace ydsh

// helper macro definition
#define WRITE(field)  writer.write(NAME(field), field)
#define WRITE_PRIM(field) writer.write(NAME(field), std::to_string(field))
#define WRITE_PTR(field) \
    do {\
        if(field == 0) {\
            writer.writeNull(NAME(field));\
        } else {\
            writer.write(NAME(field), *field);\
        }\
    } while(0)

// not directly use it.
#define GEN_ENUM_STR(ENUM, out) case ENUM: out = #ENUM; break;

#define DECODE_ENUM(out, val, EACH_ENUM) \
    do {\
        switch(val) {\
        EACH_ENUM(GEN_ENUM_STR, out)\
        }\
    } while(0)

// not directly use it.
#define GEN_FLAG_STR(FLAG, out, set) \
        if(((set) & FLAG) == FLAG) { if(c++ > 0) { out += " | "; } out += #FLAG; }

#define DECODE_BITSET(out, set, EACH_FLAG) \
    do {\
        unsigned int c = 0;\
        EACH_FLAG(GEN_FLAG_STR, out, set) \
    } while(0)


#endif /* AST_DUMP_H_ */
