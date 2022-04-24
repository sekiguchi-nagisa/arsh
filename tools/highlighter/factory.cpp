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

#include "factory.h"

namespace ydsh::highlighter {

// ##############################
// ##     FormatterFactory     ##
// ##############################

FormatterFactory::FormatterFactory(const StyleMap &map) : styleMap(std::cref(map)) {
  this->supportedFormats = {
      {"null", FormatterType::NULL_},
      {"nil", FormatterType::NULL_},
      {"empty", FormatterType::NULL_},
      {"ansi", FormatterType::TERM_TRUECOLOR},
      {"console", FormatterType::TERM_TRUECOLOR},
      {"term", FormatterType::TERM_TRUECOLOR},
      {"terminal", FormatterType::TERM_TRUECOLOR},
      {"console256", FormatterType::TERM_256},
      {"term256", FormatterType::TERM_256},
      {"terminal256", FormatterType::TERM_256},
      {"html", FormatterType::HTML},
  };
}

Result<std::unique_ptr<Formatter>, std::string>
FormatterFactory::create(std::ostream &stream) const {
  // resolve formatter
  FormatterType formatterType = ({
    auto iter = this->getSupportedFormats().find(this->formatName);
    if (iter == this->getSupportedFormats().end()) {
      std::string value = "unsupported formatter: ";
      value += this->formatName;
      return Err(std::move(value));
    }
    iter->second;
  });

  // resolve style
  const Style *style = this->styleMap.get().find(this->styleName);
  if (!style) {
    std::string value = "unsupported style: ";
    value += this->styleName;
    return Err(std::move(value));
  }

  switch (formatterType) {
  case FormatterType::NULL_:
    return Ok(std::make_unique<NullFormatter>(this->source, *style, stream));
  case FormatterType::TERM_TRUECOLOR:
  case FormatterType::TERM_256: {
    TermColorCap colorCap = formatterType == FormatterType::TERM_TRUECOLOR
                                ? TermColorCap::TRUE_COLOR
                                : TermColorCap::INDEXED_256;
    auto formatter = std::make_unique<ANSIFormatter>(this->source, *style, stream, colorCap);
    return Ok(std::move(formatter));
  }
  case FormatterType::HTML: {
    HTMLFormatOp formatOp{};
    unsigned int offset = 1;
    auto formatter =
        std::make_unique<HTMLFormatter>(this->source, *style, stream, formatOp, offset);
    return Ok(std::move(formatter));
  }
  }
  fatal("unreachable"); // normally unreachable, but suppress gcc warning
}

} // namespace ydsh::highlighter