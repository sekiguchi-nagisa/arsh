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

namespace arsh::highlighter {

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

static const HighlightTokenClass *resolveTokenClass(StringRef name) {
  for (auto &[cl, n] : getHighlightTokenRange()) {
    if (n == name && cl != HighlightTokenClass::NONE_) {
      return &cl;
    }
  }
  return nullptr;
}

static std::string applyStyleModification(const std::vector<StringRef> &customStyles,
                                          Style &style) {
  for (auto &e : customStyles) {
    auto r = e.find('=');
    if (r == StringRef::npos) {
      return "must follow `class=rule' form";
    }
    auto name = e.slice(0, r);
    auto *tokenClass = resolveTokenClass(name);
    if (!tokenClass) {
      std::string err = "undefined style class: ";
      err += name;
      return err;
    }
    auto rule = e.substr(r + 1);
    StyleRule org;
    try {
      org = org.synthesize(ValidRule(std::string_view(rule.data(), rule.size())));
    } catch (const RuleValidationError &err) {
      return err.what();
    }
    switch (*tokenClass) {
    case HighlightTokenClass::FOREGROUND_:
      style.foreground = org;
      break;
    case HighlightTokenClass::BACKGROUND_:
      style.background = org;
      break;
    default:
      style.rules[*tokenClass] = org;
      break;
    }
  }
  return "";
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
  Style style;
  if (const Style *ptr = this->styleMap.get().find(this->styleName)) {
    style = *ptr;
  } else {
    std::string value = "unsupported style: ";
    value += this->styleName;
    return Err(std::move(value));
  }

  // modify style
  if (!this->customStyles.empty()) {
    if (std::string err = applyStyleModification(this->customStyles, style); !err.empty()) {
      return Err(std::move(err));
    }
  }

  switch (formatterType) {
  case FormatterType::NULL_:
    return Ok(std::make_unique<NullFormatter>(style, stream));
  case FormatterType::TERM_TRUECOLOR:
  case FormatterType::TERM_256: {
    TermColorCap colorCap = formatterType == FormatterType::TERM_TRUECOLOR
                                ? TermColorCap::TRUE_COLOR
                                : TermColorCap::INDEXED_256;
    auto formatter = std::make_unique<ANSIFormatter>(style, this->notFoundCmds, stream, colorCap);
    return Ok(std::move(formatter));
  }
  case FormatterType::HTML: {
    HTMLFormatOp formatOp{};
    if (this->htmlFull) {
      setFlag(formatOp, HTMLFormatOp::FULL);
    }
    if (!this->lineno.empty()) {
      setFlag(formatOp, HTMLFormatOp::LINENO);
    }
    if (this->htmlTable) {
      setFlag(formatOp, HTMLFormatOp::TABLE | HTMLFormatOp::LINENO);
    }
    unsigned int lineNumOffset = 1;
    if (!this->lineno.empty()) {
      auto ret = convertToNum10<unsigned int>(this->lineno.begin(), this->lineno.end());
      if (ret && ret.value > 0) {
        lineNumOffset = ret.value;
      }
    }
    auto formatter =
        std::make_unique<HTMLFormatter>(style, this->notFoundCmds, stream, formatOp, lineNumOffset);
    return Ok(std::move(formatter));
  }
  }
  fatal("unreachable"); // normally unreachable, but suppress gcc warning
}

} // namespace arsh::highlighter