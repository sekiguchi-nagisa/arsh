/*
 * Copyright (C) 2022 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_HIGHLIGHTER_FACTORY_H
#define YDSH_TOOLS_HIGHLIGHTER_FACTORY_H

#include "formatter.h"

namespace arsh::highlighter {

enum class FormatterType {
  NULL_,
  TERM_TRUECOLOR,
  TERM_256,
  HTML,
};

class FormatterFactory {
private:
  std::reference_wrapper<const StyleMap> styleMap;

  StrRefMap<FormatterType> supportedFormats; // name to actual formatter type

  StringRef formatName{"ansi"};

  StringRef styleName{DEFAULT_STYLE_NAME};

  StringRef lineno;

  bool htmlFull{false};

  bool htmlTable{false};

public:
  static constexpr const char *DEFAULT_STYLE_NAME = "darcula";

  explicit FormatterFactory(const StyleMap &map);

  const auto &getStyleMap() const { return this->styleMap.get(); }

  const auto &getSupportedFormats() const { return this->supportedFormats; }

  void setFormatName(StringRef name) { this->formatName = name; }

  void setStyleName(StringRef name) { this->styleName = name; }

  void setLineno(StringRef num) { this->lineno = num; }

  void setHTMLFull(bool set) { this->htmlFull = set; }

  void setHTMLTable(bool set) { this->htmlTable = set; }

  Result<std::unique_ptr<Formatter>, std::string> create(std::ostream &stream) const;
};

} // namespace arsh::highlighter

#endif // YDSH_TOOLS_HIGHLIGHTER_FACTORY_H
