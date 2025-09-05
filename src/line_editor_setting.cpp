/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#include "line_buffer.h"
#include "line_editor.h"
#include "ordered_map.h"
#include "vm.h"

namespace arsh {

LineEditorObject::custom_callback_iter
LineEditorObject::lookupCustomCallback(unsigned int index) const {
  auto dummy = Value::createInvalid().withMetaData(index);
  return std::lower_bound(
      this->customCallbacks.begin(), this->customCallbacks.end(), dummy,
      [](const Value &x, const Value &y) { return x.getMetaData() < y.getMetaData(); });
}

bool LineEditorObject::defineCustomAction(ARState &state, StringRef name, StringRef type,
                                          ObjPtr<Object> &&callback) {
  if (!callback) {
    if (int index = this->keyBindings.removeCustomAction(name); index > -1) {
      auto iter = this->lookupCustomCallback(static_cast<unsigned int>(index));
      assert(iter != this->customCallbacks.end());
      this->customCallbacks.erase(iter);
    }
    return true;
  }
  auto s = this->keyBindings.defineCustomAction(name, type);
  if (s) {
    auto entry = Value(callback).withMetaData(s.asOk());
    if (auto iter = this->lookupCustomCallback(s.asOk()); iter != this->customCallbacks.end()) {
      this->customCallbacks.insert(iter, std::move(entry));
    } else {
      this->customCallbacks.push_back(std::move(entry));
    }
    return true;
  }

  std::string message;
  switch (s.asErr()) {
  case KeyBindings::DefineError::INVALID_NAME:
    message += "invalid action name, must [a-zA-Z_-]: `";
    appendAsPrintable(name, SYS_LIMIT_ERROR_MSG_MAX - 1, message);
    message += '\'';
    break;
  case KeyBindings::DefineError::INVALID_TYPE:
    message += "unsupported custom action type: `";
    appendAsPrintable(type, SYS_LIMIT_ERROR_MSG_MAX - 1, message);
    message += '\'';
    break;
  case KeyBindings::DefineError::DEFINED:
    message += "already defined action: `";
    message += name;
    message += '\'';
    break;
  case KeyBindings::DefineError::LIMIT:
    message += "number of custom actions reaches limit (up to ";
    message += std::to_string(SYS_LIMIT_CUSTOM_ACTION_MAX);
    message += ')';
    break;
  }
  raiseError(state, TYPE::ArgumentError, std::move(message));
  return false;
}

#define EACH_EDIT_CONFIG(OP)                                                                       \
  OP(USE_BRACKETED_PASTE, "bracketed-paste", TYPE::Bool)                                           \
  OP(COLOR, "color", TYPE::String)                                                                 \
  OP(EAW, "eaw", TYPE::Int)                                                                        \
  OP(USE_FLOW_CONTROL, "flow-control", TYPE::Bool)                                                 \
  OP(KEYBOARD_PROTOCOL, "keyboard-protocol", TYPE::String)                                         \
  OP(KILL_RING_SIZE, "killring-size", TYPE::Int)                                                   \
  OP(LANG_EXTENSION, "lang-extension", TYPE::Bool)                                                 \
  OP(SEMANTIC_PROMPT, "semantic-prompt", TYPE::Bool)

enum class EditConfig : unsigned char {
#define GEN_ENUM(E, S, T) E,
  EACH_EDIT_CONFIG(GEN_ENUM)
#undef GEN_ENUM
};

static const EditConfig *toEditConfig(StringRef name) {
  static const StrRefMap<EditConfig> configs = {
#define GEN_ENTRY(E, S, T) {S, EditConfig::E},
      EACH_EDIT_CONFIG(GEN_ENTRY)
#undef GEN_ENTRY
  };

  if (auto iter = configs.find(name); iter != configs.end()) {
    return &iter->second;
  }
  return nullptr;
}

static const char *toString(EditConfig config) {
  constexpr const char *table[] = {
#define GEN_TABLE(E, S, T) S,
      EACH_EDIT_CONFIG(GEN_TABLE)
#undef GEN_TABLE
  };
  return table[toUnderlying(config)];
}

static const Type &toType(const TypePool &pool, EditConfig config) {
  constexpr TYPE types[] = {
#define GEN_TABLE(E, S, T) T,
      EACH_EDIT_CONFIG(GEN_TABLE)
#undef GEN_TABLE
  };
  return pool.get(types[toUnderlying(config)]);
}

bool LineEditorObject::setConfig(ARState &state, StringRef name, const Value &value) {
  auto *config = toEditConfig(name);
  if (!config) {
    std::string message = "undefined config: `";
    appendAsPrintable(name, SYS_LIMIT_ERROR_MSG_MAX - 1, message);
    message += '\'';
    raiseError(state, TYPE::ArgumentError, std::move(message));
    return false;
  }
  if (const auto &type = toType(state.typePool, *config); type.typeId() != value.getTypeID()) {
    std::string message = "`";
    message += name;
    message += "' config require `";
    message += type.getNameRef();
    message += "' type value";
    raiseError(state, TYPE::ArgumentError, std::move(message));
    return false;
  }

  std::string message;
  switch (*config) {
  case EditConfig::KILL_RING_SIZE:
    if (auto v = value.asInt(); v < 0) {
      message += "`";
      message += name;
      message += "' config only accept positive number";
      break;
    } else {
      unsigned int cap = static_cast<unsigned int>(
          std::min(static_cast<uint64_t>(v), static_cast<uint64_t>(SYS_LIMIT_KILL_RING_MAX)));
      this->killRing.expand(cap);
      return true;
    }
  case EditConfig::EAW:
    if (auto v = value.asInt(); v == 1 || v == 2) {
      this->eaw = static_cast<unsigned char>(v);
    } else {
      this->eaw = 0;
    }
    return true;
  case EditConfig::USE_BRACKETED_PASTE:
    this->setFeature(LineEditorFeature::BRACKETED_PASTE, value.asBool());
    return true;
  case EditConfig::USE_FLOW_CONTROL:
    this->setFeature(LineEditorFeature::FLOW_CONTROL, value.asBool());
    return true;
  case EditConfig::COLOR:
    this->setColor(value.asStrRef());
    return true;
  case EditConfig::LANG_EXTENSION:
    this->setFeature(LineEditorFeature::LANG_EXTENSION, value.asBool());
    return true;
  case EditConfig::SEMANTIC_PROMPT:
    this->setFeature(LineEditorFeature::SEMANTIC_PROMPT, value.asBool());
    return true;
  case EditConfig::KEYBOARD_PROTOCOL:
    // unset related options before set option
    this->setFeature(LineEditorFeature::KITTY_KEYBOARD_PROTOCOL, false);
    this->setFeature(LineEditorFeature::XTERM_MODIFY_OTHER_KEYS, false);

    if (value.asStrRef() == "kitty") {
      this->setFeature(LineEditorFeature::KITTY_KEYBOARD_PROTOCOL, true);
    } else if (value.asStrRef() == "xterm") {
      this->setFeature(LineEditorFeature::XTERM_MODIFY_OTHER_KEYS, true);
    }
    return true;
  }
  raiseError(state, TYPE::ArgumentError, std::move(message));
  return false;
}

Value LineEditorObject::getConfigs(ARState &state) const {
  auto typeOrError =
      state.typePool.createMapType(state.typePool.get(TYPE::String), state.typePool.get(TYPE::Any));
  auto ret = Value::create<OrderedMapObject>(*typeOrError.asOk(), state.getRng().next());

  constexpr EditConfig configs[] = {
#define GEN_TABLE(E, S, T) EditConfig::E,
      EACH_EDIT_CONFIG(GEN_TABLE)
#undef GEN_TABLE
  };

  auto &map = typeAs<OrderedMapObject>(ret);
  for (auto &e : configs) {
    auto key = Value::createStr(toString(e));
    Value value;
    switch (e) {
    case EditConfig::KILL_RING_SIZE:
      value = Value::createInt(this->killRing.get().capacity());
      break;
    case EditConfig::EAW:
      value = Value::createInt(this->eaw);
      break;
    case EditConfig::USE_BRACKETED_PASTE:
      value = Value::createBool(this->hasFeature(LineEditorFeature::BRACKETED_PASTE));
      break;
    case EditConfig::USE_FLOW_CONTROL:
      value = Value::createBool(this->hasFeature(LineEditorFeature::FLOW_CONTROL));
      break;
    case EditConfig::COLOR: {
      std::string code;
      auto range = getHighlightTokenRange();
      for (auto &[cl, name] : range) {
        if (auto iter = this->escapeSeqMap.getValues().find(cl);
            iter != this->escapeSeqMap.getValues().end()) {
          if (!code.empty()) {
            code += " ";
          }
          code += name;
          code += "=";
          code += iter->second;
        }
      }
      value = Value::createStr(std::move(code));
      break;
    }
    case EditConfig::LANG_EXTENSION:
      value = Value::createBool(this->hasFeature(LineEditorFeature::LANG_EXTENSION));
      break;
    case EditConfig::SEMANTIC_PROMPT:
      value = Value::createBool(this->hasFeature(LineEditorFeature::SEMANTIC_PROMPT));
      break;
    case EditConfig::KEYBOARD_PROTOCOL:
      value =
          Value::createStr(this->hasFeature(LineEditorFeature::KITTY_KEYBOARD_PROTOCOL)   ? "kitty"
                           : this->hasFeature(LineEditorFeature::XTERM_MODIFY_OTHER_KEYS) ? "xterm"
                                                                                          : "");
      break;
    }
    map.insert(key, std::move(value));
  }
  return ret;
}

bool LineEditorObject::defineAbbr(ARState &state, StringRef pattern, StringRef expansion) {
  std::string err;
  if (pattern.empty()) {
    err = "not allow empty pattern";
  } else if (pattern.hasNullChar()) {
    err = "`pattern' contains null characters";
  } else if (expansion.hasNullChar()) {
    err = "`expansion' contains null characters";
  } else {
    auto key = pattern.toString();
    if (auto iter = this->abbrMap.find(key); iter != this->abbrMap.end()) {
      if (expansion.empty()) { // remove
        this->abbrMap.erase(iter);
      } else {
        iter->second.clear();
        iter->second += expansion; // overwrite existing abbreviation
      }
    } else if (!expansion.empty()) { // insert
      if (this->abbrMap.size() == SYS_LIMIT_ABBR_MAX) {
        err = "number of abbreviations reaches limit (up to ";
        err += std::to_string(SYS_LIMIT_ABBR_MAX);
        err += ')';
      } else {
        this->abbrMap.emplace(std::move(key), expansion.toString());
      }
    }
  }

  if (!err.empty()) {
    raiseError(state, TYPE::ArgumentError, std::move(err));
    return false;
  }
  return true;
}

} // namespace arsh