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

struct native_type_info_t;

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

    native_type_info_t *info;

public:
    TypeTemplate(std::string &&name, unsigned int elementSize, native_type_info_t *info);
    ~TypeTemplate();

    const std::string &getName();
    unsigned int getElementTypeSize();
    native_type_info_t *getInfo();
};

#endif /* CORE_TYPETEMPLATE_H_ */
