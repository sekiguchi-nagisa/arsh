/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_TEST262_HARNESS_H
#define ARSH_TOOLS_TEST262_HARNESS_H

#include <memory>

namespace arsh::re262 {

class JSEnv;

void includeHarness(const std::shared_ptr<JSEnv> &global);

} // namespace arsh::re262

#endif // ARSH_TOOLS_TEST262_HARNESS_H
