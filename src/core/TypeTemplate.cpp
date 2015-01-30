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

#include <utility>

#include <core/TypeTemplate.h>

TypeTemplate::TypeTemplate(std::string &&name, unsigned int elementSize) :
        name(std::move(name)), elementTypeSize(elementSize), init_info(), func_infos() {
}

TypeTemplate::~TypeTemplate() {
}

const std::string &TypeTemplate::getName() {
    return this->name;
}

void TypeTemplate::setNativeFuncInfoAsInit(native_func_info_t* info) {
    this->init_info = info;
}

void TypeTemplate::addNativeFuncInfo(native_func_info_t* info) {
    this->func_infos.push_back(info);
}

native_func_info_t *TypeTemplate::getInitInfo() {
    return this->init_info;
}

const std::vector<native_func_info_t*> &TypeTemplate::getFuncInfos() {
    return this->func_infos;
}

unsigned int TypeTemplate::getElementTypeSize() {
    return this->elementTypeSize;
}

