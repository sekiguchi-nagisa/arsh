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

#ifndef CORE_BIND_H_
#define CORE_BIND_H_

namespace ydsh {
namespace core {

struct native_type_info;

native_type_info_t *info_Dummy() {
    static native_type_info_t info = {0, 0, 0};
    return &info;
}


// for builtin type initialization.
native_type_info_t *info_AnyType();

native_type_info_t *info_VoidType();

native_type_info_t *info_ValueType();

native_type_info_t *info_IntType();

native_type_info_t *info_FloatType();

native_type_info_t *info_BooleanType();

native_type_info_t *info_StringType();

native_type_info_t *info_TaskType();

native_type_info_t *info_BaseFuncType();

native_type_info_t *info_ProcArgType();

native_type_info_t *info_ProcType();

// for type template initialization.
native_type_info_t *info_ArrayTemplate();

native_type_info_t *info_MapTemplate();

native_type_info_t *info_PairTemplate();

} // namespace core
} // namespace ydsh

#endif /* CORE_BIND_H_ */
