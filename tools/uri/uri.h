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

#ifndef ARSH_TOOLS_URI_URI_H
#define ARSH_TOOLS_URI_URI_H

#include <string>

namespace arsh::uri {

class Authority {
private:
  std::string userinfo;

  std::string host;

  std::string port;

public:
  Authority() = default;

  Authority(std::string &&userinfo, std::string &&host, std::string &&port)
      : userinfo(std::move(userinfo)), host(std::move(host)), port(std::move(port)) {}

  const std::string &getUserInfo() const { return this->userinfo; }

  const std::string &getHost() const { return this->host; }

  const std::string &getPort() const { return this->port; }

  explicit operator bool() const { return !this->host.empty(); }

  std::string toString() const;
};

class URI {
private:
  std::string scheme;

  Authority authority;

  std::string path;

  std::string query;

  std::string fragment;

  URI(std::string &&scheme, Authority &&authority, std::string &&path, std::string &&query,
      std::string &&fragment)
      : scheme(std::move(scheme)), authority(std::move(authority)), path(std::move(path)),
        query(std::move(query)), fragment(std::move(fragment)) {}

public:
  URI() = default;

  const std::string &getScheme() const { return this->scheme; }

  const Authority &getAuthority() const { return this->authority; }

  const std::string &getPath() const { return this->path; }

  const std::string &getQuery() const { return this->query; }

  const std::string &getFragment() const { return this->fragment; }

  explicit operator bool() const { return !this->scheme.empty(); }

  /**
   * get valid URI string
   * @return
   */
  std::string toString() const;

  /**
   * parse uri string and create URI instance
   * @param str
   * may be invalid URI string
   * @return
   */
  static URI parse(const std::string &str);

  static URI fromPath(const char *scheme, std::string &&path) {
    return {scheme, Authority(), std::move(path), "", ""};
  }

  static URI fromFilePath(std::string &&filePath) { return fromPath("file", std::move(filePath)); }

  /**
   * apply percent-encoding to string
   * @param str
   * @return
   */
  static std::string encode(const std::string &str) {
    return encode(str.c_str(), str.c_str() + str.size());
  }

  static std::string encode(const char *begin, const char *end);

  /**
   * decode percent-encoded string.
   * @param str
   * @return
   */
  static std::string decode(const std::string &str) {
    return decode(str.c_str(), str.c_str() + str.size());
  }

  static std::string decode(const char *begin, const char *end);
};

} // namespace arsh::uri

#endif // ARSH_TOOLS_URI_URI_H
