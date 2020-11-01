/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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

#ifndef YDSH_TYPE_POOL_H
#define YDSH_TYPE_POOL_H

#include "type.h"
#include "constant.h"
#include "tlerror.h"
#include "misc/buffer.hpp"
#include "misc/string_ref.hpp"
#include "misc/result.hpp"

namespace ydsh {

using TypeOrError = Result<DSType *, std::unique_ptr<TypeLookupError>>;
using TypeTempOrError = Result<const TypeTemplate*, std::unique_ptr<TypeLookupError>>;

class TypePool {
private:
    unsigned int methodIdCount{0};
    FlexBuffer<DSType *> typeTable;
    std::vector<std::string> nameTable; //   maintain type name
    std::unordered_map<std::string, unsigned int> aliasMap;

    // for reified type
    TypeTemplate arrayTemplate;
    TypeTemplate mapTemplate;
    TypeTemplate tupleTemplate;
    TypeTemplate optionTemplate;

    /**
     * for type template
     */
    std::unordered_map<std::string, const TypeTemplate *> templateMap;

    struct Key {
        unsigned int id;
        StringRef ref;

        Key(const DSType &recv, StringRef ref) : id(recv.getTypeID()), ref(ref) {}

        void dispose() {
            free(const_cast<char *>(this->ref.take()));
        }

        bool operator==(const Key &key) const {
            return this->id == key.id && this->ref == key.ref;
        }
    };

    struct Hash {
        std::size_t operator()(const Key &key) const {
            auto hash = FNVHash::compute(key.ref.begin(), key.ref.end());
            union {
                char b[4];
                unsigned int i;
            } wrap;
            wrap.i = key.id;
            for(auto b : wrap.b) {
                FNVHash::update(hash, b);
            }
            return hash;
        }
    };

    class Value {
    private:
        static constexpr uint64_t TAG = static_cast<uint64_t>(1) << 63;

        union {
            MethodHandle *handle_;
            int64_t index_;
        };

    public:
        NON_COPYABLE(Value);

        Value() : index_(0) {}

        explicit Value(unsigned int index) : index_(index | TAG) {}

        explicit Value(MethodHandle *ptr) : index_(0) {
            this->handle_ = ptr;
        }

        Value(Value &&v) noexcept : index_(v.index_) {
            v.index_ = 0;
        }

        ~Value() {
            if(this->index_ > 0) {
                delete this->handle_;
            }
        }

        Value &operator=(Value &&v) noexcept {
            std::swap(this->index_, v.index_);
            return *this;
        }

        explicit operator bool() const {
            return this->index_ > 0;
        }

        unsigned int index() const {
            return ~TAG & this->index_;
        }

        const MethodHandle *handle() const {
            return this->handle_;
        }
    };

    using MethodMap = std::unordered_map<Key, Value, Hash>;

    MethodMap methodMap;

    static constexpr unsigned int MAX_TYPE_NUM = 0xFFFFFF;

public:
    class DiscardPoint {
    private:
        friend class TypePool;

        unsigned int typeIdOffset;
        unsigned int methodIdOffset;

        DiscardPoint(unsigned int typeIdOffset, unsigned int methodIdOffset) :
            typeIdOffset(typeIdOffset), methodIdOffset(methodIdOffset) {}
    };

    NON_COPYABLE(TypePool);

    TypePool();
    ~TypePool();

    template <typename T, typename ...A>
    T &newType(std::string &&name, A &&...arg) {
        unsigned int id = this->typeTable.size();
        return *static_cast<T *>(this->addType(std::move(name), new T(id, std::forward<A>(arg)...)));
    }

    DSType &get(TYPE t) const {
        return this->get(static_cast<unsigned int>(t));
    }

    DSType &get(unsigned int index) const {
        return *this->typeTable[index];
    }

    /**
     * return null, if has no type.
     */
    TypeOrError getType(const std::string &typeName) const;

    /**
     * type must not be null.
     */
    const std::string &getTypeName(const DSType &type) const {
        return this->nameTable[type.getTypeID()];
    }

    const char *getTypeNameCStr(const DSType &type) const {
        return this->getTypeName(type).c_str();
    }

    const TypeTemplate &getArrayTemplate() const {
        return this->arrayTemplate;
    }

    const TypeTemplate &getMapTemplate() const {
        return this->mapTemplate;
    }

    const TypeTemplate &getTupleTemplate() const {
        return this->tupleTemplate;
    }

    const TypeTemplate &getOptionTemplate() const {
        return this->optionTemplate;
    }

    bool isArrayType(const DSType &type) const {
        return type.isReifiedType() &&
            static_cast<const ReifiedType&>(type).getNativeTypeInfo() == this->arrayTemplate.getInfo();
    }

    bool isMapType(const DSType &type) const {
        return type.isReifiedType() &&
            static_cast<const ReifiedType&>(type).getNativeTypeInfo() == this->mapTemplate.getInfo();
    }

    bool isTupleType(const DSType &type) const {
        return type.isReifiedType() &&
            static_cast<const ReifiedType&>(type).getNativeTypeInfo() == this->tupleTemplate.getInfo();
    }

    TypeTempOrError getTypeTemplate(const std::string &typeName) const;

    TypeOrError createReifiedType(const TypeTemplate &typeTemplate, std::vector<DSType *> &&elementTypes);

    TypeOrError createArrayType(DSType &elementType) {
        return this->createReifiedType(this->getArrayTemplate(), {&elementType});
    }

    TypeOrError createMapType(DSType &keyType, DSType &valueType) {
        return this->createReifiedType(this->getMapTemplate(), {&keyType, &valueType});
    }

    TypeOrError createOptionType(DSType &elementType) {
        return this->createReifiedType(this->getOptionTemplate(), {&elementType});
    }

    TypeOrError createTupleType(std::vector<DSType *> &&elementTypes);

    /**
     *
     * @param returnType
     * @param paramTypes
     * @return
     * must be FunctionType
     */
    TypeOrError createFuncType(DSType *returnType, std::vector<DSType *> &&paramTypes);

    /**
     * return false, if duplicated
     */
    bool setAlias(std::string &&alias, const DSType &type) {
        auto pair = this->aliasMap.emplace(std::move(alias), type.getTypeID());
        return pair.second;
    }

    const MethodHandle *lookupMethod(const DSType &recvType, const std::string &methodName);

    const MethodHandle *lookupMethod(unsigned int typeId, const std::string &methodName) {
        return this->lookupMethod(this->get(typeId), methodName);
    }

    const MethodHandle *lookupMethod(TYPE type, const std::string &methodName) {
        return this->lookupMethod(static_cast<unsigned int>(type), methodName);
    }

    const MethodHandle *lookupConstructor(const DSType &revType) {
        return this->lookupMethod(revType, "");
    }

    DiscardPoint getDiscardPoint() const {
        return DiscardPoint(this->typeTable.size(), this->methodIdCount);
    }

    void discard(DiscardPoint point);

private:
    DSType *get(const std::string &typeName) const {
        auto iter = this->aliasMap.find(typeName);
        if(iter == this->aliasMap.end()) {
            return nullptr;
        }
        return &this->get(iter->second);
    }

    /**
     * return added type. type must not be null.
     */
    DSType *addType(std::string &&typeName, DSType *type);

    /**
     * create reified type name
     * equivalent to toReifiedTypeName(typeTemplate->getName(), elementTypes)
     */
    std::string toReifiedTypeName(const TypeTemplate &typeTemplate, const std::vector<DSType *> &elementTypes) const;

    std::string toTupleTypeName(const std::vector<DSType *> &elementTypes) const;

    /**
     * create function type name
     */
    std::string toFunctionTypeName(DSType *returnType, const std::vector<DSType *> &paramTypes) const;


    /**
     *
     * @param elementTypes
     * @return
     * if success, return null
     */
    TypeOrError checkElementTypes(const std::vector<DSType *> &elementTypes) const;

    /**
     *
     * @param t
     * @param elementTypes
     * @return
     * if success, return null
     */
    TypeOrError checkElementTypes(const TypeTemplate &t, const std::vector<DSType *> &elementTypes) const;

    void initBuiltinType(TYPE t, const char *typeName, bool extendible, native_type_info_t info) {
        this->initBuiltinType(t, typeName, extendible, nullptr, info);
    }

    void initBuiltinType(TYPE t, const char *typeName, bool extendible, TYPE super, native_type_info_t info) {
        this->initBuiltinType(t, typeName, extendible, &this->get(super), info);
    }

    void initBuiltinType(TYPE t, const char *typeName, bool extendible, const DSType *super, native_type_info_t info);

    void initTypeTemplate(TypeTemplate &temp, const char *typeName,
                          std::vector<DSType*> &&elementTypes, native_type_info_t info);

    void initErrorType(TYPE t, const char *typeName);

    void registerHandle(const BuiltinType &recv, const char *name, unsigned int index);

    void registerHandles(const BuiltinType &type);

    /**
     * allocate actual MethodHandle
     * @param recv
     * @param iter
     * after allocation, set allocated method handle pointer.
     * @return
     * if allocation failed, return false
     */
    bool allocMethodHandle(const DSType &recv, MethodMap::iterator iter);
};


} // namespace ydsh

#endif //YDSH_TYPE_POOL_H
