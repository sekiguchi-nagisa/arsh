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

namespace ydsh {

bool LineEditorObject::addKeyBind(DSState &state, StringRef key, StringRef name) {
  auto s = this->keyBindings.addBinding(key, name);
  std::string message;
  switch (s) {
  case KeyBindings::AddStatus::OK:
    break;
  case KeyBindings::AddStatus::UNDEF:
    message = "undefined edit action: `";
    message += toPrintable(name);
    message += "'";
    break;
  case KeyBindings::AddStatus::FORBID_BRACKET_START_CODE:
    message = "cannot change binding of bracket start code `";
    message += KeyBindings::toCaret(KeyBindings::BRACKET_START);
    message += "'";
    break;
  case KeyBindings::AddStatus::FORBID_BRACKET_ACTION:
    message = "cannot bind to `";
    message += toString(EditActionType::BRACKET_PASTE);
    message += "'";
    break;
  case KeyBindings::AddStatus::INVALID_START_CHAR:
    message = "keycode must start with control character: `";
    message += toPrintable(key);
    message += "'";
    break;
  case KeyBindings::AddStatus::INVALID_ASCII:
    message = "keycode must be ascii characters: `";
    message += toPrintable(key);
    message += "'";
    break;
  case KeyBindings::AddStatus::LIMIT:
    message = "number of key bindings reaches limit (up to ";
    message += std::to_string(SYS_LIMIT_KEY_BINDING_MAX);
    message += ")";
    break;
  }
  if (!message.empty()) {
    raiseError(state, TYPE::ArgumentError, std::move(message));
    return false;
  }
  return true;
}

bool LineEditorObject::defineCustomAction(DSState &state, StringRef name, StringRef type,
                                          ObjPtr<DSObject> callback) {
  auto s = this->keyBindings.defineCustomAction(name, type);
  if (s) {
    assert(this->customCallbacks.size() == s.asOk());
    this->customCallbacks.push_back(std::move(callback));
    return true;
  }

  std::string message;
  switch (s.asErr()) {
  case KeyBindings::DefineError::INVALID_NAME:
    message += "invalid action name, must [a-zA-Z_-]: `";
    message += toPrintable(name);
    message += "'";
    break;
  case KeyBindings::DefineError::INVALID_TYPE:
    message += "unsupported custom action type: `";
    message += toPrintable(type);
    message += "'";
    break;
  case KeyBindings::DefineError::DEFINED:
    message += "already defined action: `";
    message += name;
    message += "'";
    break;
  case KeyBindings::DefineError::LIMIT:
    message += "number of custom actions reaches limit (up to ";
    message += std::to_string(SYS_LIMIT_CUSTOM_ACTION_MAX);
    message += ")";
    break;
  }
  raiseError(state, TYPE::ArgumentError, std::move(message));
  return false;
}

#define EACH_EDIT_CONFIG(OP)                                                                       \
  OP(KILL_RING_SIZE, "killring-size", TYPE::Int)                                                   \
  OP(USE_BRACKETED_PASTE, "bracketed-paste", TYPE::Bool)                                           \
  OP(USE_FLOW_CONTROL, "flow-control", TYPE::Bool)                                                 \
  OP(COLOR, "color", TYPE::String)

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
  const char *table[] = {
#define GEN_TABLE(E, S, T) S,
      EACH_EDIT_CONFIG(GEN_TABLE)
#undef GEN_TABLE
  };
  return table[toUnderlying(config)];
}

static const DSType &toType(const TypePool &pool, EditConfig config) {
  const TYPE types[] = {
#define GEN_TABLE(E, S, T) T,
      EACH_EDIT_CONFIG(GEN_TABLE)
#undef GEN_TABLE
  };
  return pool.get(types[toUnderlying(config)]);
}

bool LineEditorObject::setConfig(DSState &state, StringRef name, const DSValue &value) {
  auto *config = toEditConfig(name);
  if (!config) {
    std::string message = "undefined config: `";
    message += toPrintable(name);
    message += "'";
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
  case EditConfig::USE_BRACKETED_PASTE:
    this->useBracketedPaste = value.asBool();
    return true;
  case EditConfig::USE_FLOW_CONTROL:
    this->useFlowControl = value.asBool();
    return true;
  case EditConfig::COLOR:
    this->setColor(value.asStrRef());
    return true;
  }
  raiseError(state, TYPE::ArgumentError, std::move(message));
  return false;
}

DSValue LineEditorObject::getConfigs(DSState &state) const {
  auto typeOrError =
      state.typePool.createMapType(state.typePool.get(TYPE::String), state.typePool.get(TYPE::Any));
  auto ret = DSValue::create<OrderedMapObject>(*typeOrError.asOk(), state.getRng().next());

  const EditConfig configs[] = {
#define GEN_TABLE(E, S, T) EditConfig::E,
      EACH_EDIT_CONFIG(GEN_TABLE)
#undef GEN_TABLE
  };

  auto &map = typeAs<OrderedMapObject>(ret);
  for (auto &e : configs) {
    auto key = DSValue::createStr(toString(e));
    DSValue value;
    switch (e) {
    case EditConfig::KILL_RING_SIZE:
      value = DSValue::createInt(this->killRing.get().capacity());
      break;
    case EditConfig::USE_BRACKETED_PASTE:
      value = DSValue::createBool(this->useBracketedPaste);
      break;
    case EditConfig::USE_FLOW_CONTROL:
      value = DSValue::createBool(this->useFlowControl);
      break;
    case EditConfig::COLOR: {
      std::string code;
      auto &entries = getHighlightTokenEntries();
      for (auto &entry : entries) {
        if (auto iter = this->escapeSeqMap.getValues().find(entry.first);
            iter != this->escapeSeqMap.getValues().end()) {
          if (!code.empty()) {
            code += " ";
          }
          code += entry.second;
          code += "=";
          code += iter->second;
        }
      }
      value = DSValue::createStr(std::move(code));
      break;
    }
    }
    map.insert(key, std::move(value));
  }
  return ret;
}

} // namespace ydsh