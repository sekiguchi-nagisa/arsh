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

#include "magic_method.h"

const std::string PLUS  = "__PLUS__";
const std::string MINUS = "__MINUS__";
const std::string NOT   = "__NOT__";

// binary op definition
const std::string ADD   = "__ADD__";
const std::string SUB   = "__SUB__";
const std::string MUL   = "__MUL__";
const std::string DIV   = "__DIV__";
const std::string MOD   = "__MOD__";

const std::string EQ    = "__EQ__";
const std::string NE    = "__NE__";

const std::string LT    = "__LT__";
const std::string GT    = "__GT__";
const std::string LE    = "__LE__";
const std::string GE    = "__GE__";

const std::string AND   = "__AND__";
const std::string OR    = "__OR__";
const std::string XOR   = "__XOR__";

const std::string RE_EQ = "__RE_EQ__";
const std::string RE_NE = "__RE_NE__";

// indexer op
const std::string GET   = "__GET__";
const std::string SET   = "__SET__";

// iterator
const std::string RESET    = "__RESET__";
const std::string NEXT     = "__NEXT__";
const std::string HAS_NEXT = "__HAS_NEXT__";

