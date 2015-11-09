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

#ifndef YDSH_BIND_H
#define YDSH_BIND_H

#include "DSType.h"

namespace ydsh {
namespace core {

native_type_info_t info_Dummy();

// for builtin type initialization.
native_type_info_t info_AnyType();
native_type_info_t info_VoidType();
native_type_info_t info_ByteType();
native_type_info_t info_Int16Type();
native_type_info_t info_Uint16Type();
native_type_info_t info_Int32Type();
native_type_info_t info_Uint32Type();
native_type_info_t info_Int64Type();
native_type_info_t info_Uint64Type();
native_type_info_t info_FloatType();
native_type_info_t info_BooleanType();
native_type_info_t info_StringType();
native_type_info_t info_ObjectPathType();
native_type_info_t info_UnixFDType();
native_type_info_t info_DBusType();
native_type_info_t info_BusType();
native_type_info_t info_ServiceType();
native_type_info_t info_DBusObjectType();
native_type_info_t info_ErrorType();
native_type_info_t info_ProxyType();
native_type_info_t info_StringIterType();

// for type template initialization.
native_type_info_t info_ArrayType();
native_type_info_t info_MapType();
native_type_info_t info_TupleType();

} // namespace core
} // namespace ydsh

#endif //YDSH_BIND_H
