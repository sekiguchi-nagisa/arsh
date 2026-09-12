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

#include <cstdio>
#include <cstring>

#include "object_file.h"

namespace arsh::gen_stencil {

bool readFile(const StringRef path, std::vector<char> &out, std::string &error) {
  FILE *fp = fopen(path.toString().c_str(), "rb");
  if (!fp) {
    error = "cannot open: " + path.toString();
    return false;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    error = "cannot seek: " + path.toString();
    return false;
  }
  const long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    error = "cannot tell: " + path.toString();
    return false;
  }
  rewind(fp);
  out.resize(static_cast<size_t>(size));
  if (size > 0 && fread(out.data(), 1, out.size(), fp) != out.size()) {
    fclose(fp);
    error = "cannot read: " + path.toString();
    return false;
  }
  fclose(fp);
  return true;
}

} // namespace arsh::gen_stencil
