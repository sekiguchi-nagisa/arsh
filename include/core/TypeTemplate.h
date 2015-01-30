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

#ifndef CORE_TYPETEMPLATE_H_
#define CORE_TYPETEMPLATE_H_

#include <string>
#include <vector>

struct native_func_info_t;

/**
 * ReifiedType template.
 */
class TypeTemplate {
private:
    std::string name;

    /**
     * if size is 0, allow infinite element type.(for Tuple)
     */
    unsigned int elementTypeSize;

    /**
     * may be null, if has no constructor
     */
    native_func_info_t *init_info;
    std::vector<native_func_info_t*> func_infos;

public:
    TypeTemplate(std::string &&name, unsigned int elementSize);
    ~TypeTemplate();

    const std::string &getName();
    void setNativeFuncInfoAsInit(native_func_info_t* info);
    void addNativeFuncInfo(native_func_info_t* info);
    native_func_info_t *getInitInfo();
    const std::vector<native_func_info_t*> &getFuncInfos();
    unsigned int getElementTypeSize();
};

#endif /* CORE_TYPETEMPLATE_H_ */
