/*
 * Copyright (C) 2018 Nagisa Sekiguchi
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

#include "uri.h"

#include <misc/num_util.hpp>

namespace ydsh::uri {

std::string Authority::toString() const {
  std::string value;
  if (!this->userinfo.empty()) {
    value += URI::encode(this->userinfo);
    value += '@';
  }
  value += URI::encode(this->host);
  if (!this->port.empty()) {
    value += ':';
    value += URI::encode(this->port);
  }
  return value;
}

std::string URI::toString() const {
  std::string value = this->scheme;
  value += ':';
  if (this->authority || (!this->path.empty() && this->path[0] == '/')) {
    value += "//";
    value += this->authority.toString();
  }
  value += URI::encode(this->path);
  if (!this->query.empty()) {
    value += '?';
    value += URI::encode(this->query);
  }
  if (!this->fragment.empty()) {
    value += '#';
    value += URI::encode(this->fragment);
  }
  return value;
}

static bool isEscaped(int ch) {
  switch (ch) {
  case '.':
  case '_':
  case '~':
    return false;
  case '/':
    return false; // FIXME:
  default:
    if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
      return false;
    }
  }
  return true;
}

std::string URI::encode(const char *begin, const char *end) {
  std::string value;
  for (; begin != end; ++begin) {
    char ch = *begin;
    if (isEscaped(ch)) {
      value += '%';
      char buf[16];
      snprintf(buf, 16, "%02X", static_cast<unsigned char>(ch));
      value += buf;
    } else {
      value += ch;
    }
  }
  return value;
}

std::string URI::decode(const char *begin, const char *end) {
  std::string value;
  for (; begin != end; ++begin) {
    char ch = *begin;
    if (ch != '%') {
      value += ch;
    } else if (begin + 2 < end && isHex(*(begin + 1)) && isHex(*(begin + 2))) {
      char ch1 = *++begin;
      char ch2 = *++begin;
      unsigned int v = 16 * hexToNum(ch1) + hexToNum(ch2);
      value += static_cast<char>(v);
    } else {
      value += ch;
    }
  }
  return value;
}

} // namespace ydsh::uri