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

struct native_func_info_t;

typedef struct {
    unsigned int infoSize;
    native_func_info_t **infos;
} native_func_infos_t;

const native_func_infos_t &info_Dummy() {
    static native_func_infos_t dummy = {0, 0};
    return dummy;
}


// for builtin type initialization.
const native_func_infos_t &info_AnyType();
const native_func_infos_t &info_VoidType();

const native_func_infos_t &info_ValueType();

const native_func_infos_t &info_IntType();
const native_func_infos_t &info_FloatType();
const native_func_infos_t &info_BooleanType();
const native_func_infos_t &info_StringType();
const native_func_infos_t &info_TaskType();
const native_func_infos_t &info_BaseFuncType();

const native_func_infos_t &info_ProcArgType();
const native_func_infos_t &info_ProcType();

// for type template initialization.
const native_func_infos_t &info_ArrayTemplate();
const native_func_infos_t &info_MapTemplate();
const native_func_infos_t &info_PairTemplate();


#endif /* CORE_BIND_H_ */
