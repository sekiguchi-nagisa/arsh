/*
 * Copyright (C) 2024 Nagisa Sekiguchi
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

#include <iostream>

#include <misc/files.hpp>

using namespace arsh;

static int changeStdinToNonblock(const char *const *, const char *const *) {
  return setFDFlag(STDIN_FILENO, O_NONBLOCK, true) ? 0 : 1;
}

using Command = int (*)(const char *const *, const char *const *);

static struct {
  StringRef name;
  Command cmd;
} table[] = {{"nonblock-in", changeStdinToNonblock}};

static Command resolve(StringRef name) {
  for (auto &[n, c] : table) {
    if (n == name) {
      return c;
    }
  }
  return nullptr;
}

int main(int argc, char **argv) {
  const char *const *begin = argv + 1;
  const char *const *end = argv + argc;
  if (begin != end) {
    if (auto cmd = resolve(*begin)) {
      ++begin;
      return cmd(begin, end);
    }
    std::cerr << "undefined command: " << *begin << '\n' << "Commands:\n";
    for (auto &e : table) {
      std::cerr << e.name.toString() << '\n';
    }
    return 1;
  }
  return 0;
}