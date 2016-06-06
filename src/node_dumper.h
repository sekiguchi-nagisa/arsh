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

#ifndef YDSH_NODE_DUMPER_H
#define YDSH_NODE_DUMPER_H

#define NAME(f) #f

#include <string>
#include <ostream>
#include <vector>
#include <list>
#include <type_traits>

namespace ydsh {

class TypePool;
class DSType;

};

namespace ydsh {

class Node;
class TypeNode;
class RootNode;

class NodeDumper {
private:
    std::ostream &stream;
    TypePool &pool;

    unsigned int indentLevel;

public:
    NodeDumper(std::ostream &stream, TypePool &pool) :
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

    void dump(const char *fieldName, const DSType &type);

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

    void leaveIndent();

    void indent();

    void dumpNodeHeader(const Node &node, bool inArray = false);

    void writeName(const char *fieldName);
};

template <typename T>
inline std::vector<Node *> toNodes(const std::vector<T *> &nodes) {
    static_assert(std::is_base_of<Node, T>::value, "must be subtype of Node");
    std::vector<Node *> v;
    for(const auto &e : nodes) {
        v.push_back(e);
    }
    return v;
}

} // namespace ydsh

// helper macro definition
#define DUMP(field) dumper.dump(NAME(field), field)
#define DUMP_PRIM(field) dumper.dump(NAME(field), std::to_string(field))
#define DUMP_PTR(field) \
    do {\
        if(field == nullptr) {\
            dumper.dumpNull(NAME(field));\
        } else {\
            dumper.dump(NAME(field), *field);\
        }\
    } while(false)

#define DUMP_NODES(field) dumper.dump(NAME(field), toNodes(field))


// not directly use it.
#define GEN_ENUM_STR(ENUM) case ENUM: ___str__ = #ENUM; break;

#define DUMP_ENUM(val, EACH_ENUM) \
    do {\
        const char *___str__ = nullptr;\
        switch(val) {\
        EACH_ENUM(GEN_ENUM_STR)\
        }\
        dumper.dump(NAME(val), ___str__);\
    } while(false)

// not directly use it.
#define GEN_FLAG_STR(FLAG) \
        if((___set__ & FLAG)) { if(___count__++ > 0) { ___str__ += " | "; } ___str__ += #FLAG; }

#define DUMP_BITSET(val, EACH_FLAG) \
    do {\
        unsigned int ___count__ = 0;\
        std::string ___str__;\
        auto ___set__ = val;\
        EACH_FLAG(GEN_FLAG_STR)\
        dumper.dump(NAME(val), ___str__);\
    } while(false)

#endif //YDSH_NODE_DUMPER_H
