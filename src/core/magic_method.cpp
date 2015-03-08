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

#include <core/magic_method.h>

const char * const OP_PLUS  = "__PLUS__";
const char * const OP_MINUS = "__MINUS__";
const char * const OP_NOT   = "__NOT__";

// binary op definition
const char * const OP_ADD   = "__ADD__";
const char * const OP_SUB   = "__SUB__";
const char * const OP_MUL   = "__MUL__";
const char * const OP_DIV   = "__DIV__";
const char * const OP_MOD   = "__MOD__";

const char * const OP_EQ    = "__EQ__";
const char * const OP_NE    = "__NE__";

const char * const OP_LT    = "__LT__";
const char * const OP_GT    = "__GT__";
const char * const OP_LE    = "__LE__";
const char * const OP_GE    = "__GE__";

const char * const OP_AND   = "__AND__";
const char * const OP_OR    = "__OR__";
const char * const OP_XOR   = "__XOR__";

const char * const OP_RE_EQ = "__RE_EQ__";
const char * const OP_RE_NE = "__RE_NE__";

// indexer op
const char * const OP_GET   = "__GET__";
const char * const OP_SET   = "__SET__";

// iterator
const char * const OP_RESET    = "__RESET__";
const char * const OP_NEXT     = "__NEXT__";
const char * const OP_HAS_NEXT = "__HAS_NEXT__";

// to string
const char * const OP_TO_STR   = "__TO_STR__";
const char * const OP_INTERP   = "__INTERP__";
