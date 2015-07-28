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

#ifndef TOOLS_INTROSPECTOR_H
#define TOOLS_INTROSPECTOR_H

#include <string>

struct Introspector {
    bool operator!() const;

    /**
     * return true, if available D-Bus support.
     */
    bool available() const;

    /**
     * if introspection failed, return empty string.
     */
    std::string operator()(bool isSystemBus,
                           const std::string &serviceName, const std::string &path) const;
};

#endif //TOOLS_INTROSPECTOR_H
