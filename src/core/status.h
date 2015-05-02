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

#ifndef CORE_STATUS_H
#define CORE_STATUS_H

/**
 * definition of some return status
 */

namespace ydsh {
namespace core {

enum EvalStatus {
    EVAL_SUCCESS,
    EVAL_BREAK,
    EVAL_CONTINUE,
    EVAL_THROW,
    EVAL_RETURN,
    EVAL_REMOVE,
};

enum ExitStatus {
    SUCCESS,
    ARGS_ERROR,
    IO_ERROR,
    PARSE_ERROR,
    TYPE_ERROR,
    RUNTIME_ERROR,
    ASSERTION_ERROR,
};


} // namespace core
} // namespace ydsh

#endif //CORE_STATUS_H
