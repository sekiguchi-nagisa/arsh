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

#include <variant>

#include "meta.h"
#include "misc/format.hpp"

namespace arsh::re262 {

// ##########################
// ##     TestMetaData     ##
// ##########################

static StringRef extractMetaDataSection(StringRef input) {
  if (const auto startPos = input.find("/*---"); startPos != StringRef::npos) {
    auto sub = input.substr(startPos + strlen("/*---"));
    if (const auto endPos = sub.find("---*/"); endPos != StringRef::npos) {
      return sub.slice(0, endPos);
    }
  }
  return "";
}

static bool isSpace(char ch) {
  switch (ch) {
  case ' ':
  case '\t':
  case '\r':
  case '\n':
    return true;
  default:
    return false;
  }
}

static StringRef trim(StringRef ref) {
  while (!ref.empty() && isSpace(ref[0])) {
    ref.removePrefix(1);
  }
  while (!ref.empty() && isSpace(ref.back())) {
    ref.removeSuffix(1);
  }
  return ref;
}

static std::string readUntilIndentEnd(const std::vector<StringRef> &lines, unsigned int &index,
                                      char join) {
  std::string ret;
  unsigned int count = 0;
  for (; index < lines.size(); index++) {
    StringRef line = lines[index];
    if (!line.empty() && !line.startsWith(" ") && !line.startsWith("\t")) {
      index--;
      break;
    }
    if (count++ > 0) {
      if (join == ' ') {
        if (line.empty()) {
          ret += '\n';
        } else if (!ret.empty() && ret.back() != '\n') {
          ret += join;
        }
      } else {
        ret += join;
      }
    }
    ret += trim(line);
  }
  return ret;
}

static std::optional<TestMetaData::Negative> parseNegative(const std::vector<StringRef> &lines,
                                                           unsigned int &index, std::string *err) {
  TestMetaData::Negative negative;
  std::optional<TestMetaData::Phase> phase;
  std::string type;
  for (unsigned int count = 0; count < 2 && index < lines.size(); index++) {
    if (auto tmp = lines[index]; tmp.empty()) {
      break;
    } else if (!tmp.startsWith(" ") && !tmp.startsWith("\t")) {
      if (err) {
        *err += "attribute must start with indent in negative:\n";
        *err += tmp;
      }
      return {};
    }
    auto line = trim(lines[index]);
    auto retPos = line.find(':');
    if (retPos == StringRef::npos) {
      if (err) {
        *err += "unrecognized attribute in negative:\n";
        *err += line;
      }
      return {};
    }
    auto key = trim(line.slice(0, retPos));
    auto value = trim(line.substr(retPos + 1));
    if (key == "phase") {
      if (value == "parse") {
        phase = TestMetaData::Phase::PARSE;
      } else if (value == "runtime") {
        phase = TestMetaData::Phase::RUNTIME;
      } else {
        if (err) {
          *err += "unrecognized value in phase: ";
          *err += value;
          *err += ", must be `parse' or `runtime'";
        }
        return {};
      }
    } else if (key == "type") {
      type = value.toString();
    } else {
      if (err) {
        *err += "unrecognized attribute in negative:\n";
        *err += line;
      }
      return {};
    }
    count++;
  }
  index--;
  if (phase.has_value() && !type.empty()) {
    return TestMetaData::Negative{.phase = phase.value(), .type = std::move(type)};
  }
  if (err) {
    *err += "phase or type are not provided";
  }
  return {};
}

std::optional<TestMetaData> TestMetaData::extractFrom(const StringRef input, std::string *err) {
  auto section = extractMetaDataSection(input);
  if (section.empty()) {
    if (err) {
      *err += "cannot extract meta data section";
    }
    return {};
  }
  std::vector<StringRef> lines;
  splitByDelim(section, '\n', [&lines](StringRef line, bool) {
    lines.push_back(line);
    return true;
  });

  TestMetaData meta;
  unsigned int index = 0;
  for (; index < lines.size(); index++) {
    StringRef line = trim(lines[index]);
    if (line.empty() || line.startsWith("#")) {
      continue;
    }
    auto sepPos = line.find(':');
    if (sepPos == StringRef::npos) {
      if (err) {
        *err += "unrecognized line in meta-data section\n";
        *err += line;
      }
      return {};
    }
    const auto key = trim(line.slice(0, sepPos));
    auto tmp = trim(line.substr(sepPos + 1));
    std::variant<std::string, std::vector<std::string>> value;
    if (tmp == ">") {
      index++;
      value = readUntilIndentEnd(lines, index, ' ');
    } else if (tmp == "|") {
      index++;
      value = readUntilIndentEnd(lines, index, '\n');
    } else if (tmp.startsWith("[") && tmp.endsWith("]")) { // array
      tmp.removePrefix(1);
      tmp.removeSuffix(1);
      std::vector<std::string> values;
      splitByDelim(tmp, ',', [&values](StringRef v, bool) {
        values.push_back(trim(v).toString());
        return true;
      });
      value = std::move(values);
    } else {
      value = tmp.toString();
    }

    // store meta-data
    if (key == "author" && std::holds_alternative<std::string>(value)) {
      meta.author = std::move(std::get<std::string>(value));
    } else if (key == "description" && std::holds_alternative<std::string>(value)) {
      meta.description = std::move(std::get<std::string>(value));
    } else if (key == "info" && std::holds_alternative<std::string>(value)) {
      meta.info = std::move(std::get<std::string>(value));
    } else if (key == "esid" && std::holds_alternative<std::string>(value)) {
      meta.esid = std::move(std::get<std::string>(value));
    } else if (key == "features" && std::holds_alternative<std::vector<std::string>>(value)) {
      meta.features = std::move(std::get<std::vector<std::string>>(value));
    } else if (key == "includes" && std::holds_alternative<std::vector<std::string>>(value)) {
      meta.includes = std::move(std::get<std::vector<std::string>>(value));
    } else if (key == "negative" && tmp.empty()) {
      index++;
      auto negative = parseNegative(lines, index, err);
      if (!negative.has_value()) {
        return {};
      }
      meta.negative = std::move(negative);
    } else {
      if (err) {
        *err += "unrecognized attribute: ";
        *err += key;
        *err += '\n';
        *err += line;
      }
      return {};
    }
  }
  return meta;
}

} // namespace arsh::re262