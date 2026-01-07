/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#include "property.h"
#include "../misc/codepoint_set.hpp"
#include "../misc/enum_util.hpp"
#include "../misc/format.hpp"
#include "../misc/unicode.hpp"

namespace arsh::ucp {

#define PROPERTY_SET_RANGE BMPCodePointRange
#define PROPERTY_SET_RANGE_TABLE CodePointSetRef

// ==============================
// ==     General Category     ==
// ==============================

#include "ucp_general_category.in"

static constexpr const char *categoryNames[] = {
#define GEN_TABLE(E, S) S,
    EACH_UCP_GENERAL_CATEGORY(GEN_TABLE)
#undef GEN_TABLE
};

static auto initCategoryNameMap() {
  StrRefMap<Category> map;
  for (unsigned int i = 0; i < std::size(categoryNames); i++) {
    auto category = static_cast<Category>(i);
    splitByDelim(categoryNames[i], '|', [&map, category](StringRef ref, bool) {
      map.emplace(ref, category);
      return true;
    });
  }
  return map;
}

Optional<Category> parseCategory(const StringRef ref) {
  static const auto map = initCategoryNameMap();
  if (const auto iter = map.find(ref); iter != map.end()) {
    return iter->second;
  }
  return {};
}

Optional<Category> getCategory(const int codePoint) {
  if (UnicodeUtil::isValidCodePoint(codePoint)) {
    for (unsigned int i = 0; i < std::size(category_set_table_except_Cn); i++) {
      if (category_set_table_except_Cn[i].contains(codePoint)) {
        return static_cast<Category>(i);
      }
    }
    return Category::Cn;
  }
  return {};
}

StringRef toString(Category category, bool longName) {
  if (const auto i = toUnderlying(category); i < std::size(categoryNames)) {
    StringRef name;
    unsigned int count = 0;
    const unsigned int limit = longName ? 2 : 1;
    splitByDelim(categoryNames[i], '|', [&count, &limit, &name](StringRef ref, bool) {
      name = ref;
      return ++count < limit;
    });
    return name;
  }
  return "";
}

static constexpr struct CategoryCombination {
  Category category;
  Category combination[7]; // Cn is sentinel
} categoryCombinations[] = {
    {Category::LC, {Category::Lu, Category::Ll, Category::Lt, Category::Cn}},
    {Category::L,
     {Category::Lu, Category::Ll, Category::Lt, Category::Lm, Category::Lo, Category::Cn}},
    {Category::M, {Category::Mn, Category::Mc, Category::Me, Category::Cn}},
    {Category::N, {Category::Nd, Category::Nl, Category::No, Category::Cn}},
    {Category::P,
     {Category::Pc, Category::Pd, Category::Ps, Category::Pe, Category::Pi, Category::Pf,
      Category::Po}},
    {Category::S, {Category::Sm, Category::Sc, Category::Sk, Category::So, Category::Cn}},
    {Category::Z, {Category::Zs, Category::Zl, Category::Zp, Category::Cn}},
};

static CategoryCombination lookupCategoryCombination(const Category category) {
  for (auto &e : categoryCombinations) {
    if (e.category == category) {
      return e;
    }
  }
  return {Category::Cn, {Category::Cn}}; // dummy
}

static bool getCategorySet(const Category category, BuilderOrSet out) {
  CodePointSet set;
  switch (category) {
#define GEN_CASE(E, S) case Category::E:
    EACH_UCP_GENERAL_CATEGORY_PRIME(GEN_CASE)
#undef GEN_CASE
    if (auto ref = category_set_table_except_Cn[toUnderlying(category)]; out.isBuilder) {
      out.builder->add(ref);
    } else {
      *out.set = CodePointSet::borrow(ref);
    }
    return true;
  case Category::LC:
  case Category::L:
  case Category::M:
  case Category::N:
  case Category::P:
  case Category::S:
  case Category::Z: {
    CodePointSetBuilder builder;
    CodePointSetBuilder *b = &builder;
    if (out.isBuilder) {
      b = out.builder;
    }
    for (auto c : lookupCategoryCombination(category).combination) {
      if (unsigned int index = toUnderlying(c); index < std::size(category_set_table_except_Cn)) {
        b->add(category_set_table_except_Cn[index]);
      }
    }
    if (!out.isBuilder) {
      *out.set = b->build();
    }
    return true;
  }
  case Category::Cn:
  case Category::C: {
    // build Cn
    CodePointSetBuilder builder;
    for (auto c : category_set_table_except_Cn) {
      builder.add(c);
    }
    builder.complement();

    if (category == Category::C) {
      for (auto c : {Category::Cc, Category::Cf, Category::Cs, Category::Co}) {
        builder.add(category_set_table_except_Cn[toUnderlying(c)]);
      }
    }
    if (auto tmp = builder.build(); out.isBuilder) {
      out.builder->add(tmp.ref());
    } else {
      *out.set = std::move(tmp);
    }
    return true;
  }
  }
  return false; // normally unreachable (for broken category)
}

// ====================
// ==     Script     ==
// ====================

#include "ucp_script.in"

static constexpr const char *scriptNames[] = {
#define GEN_TABLE(E, S) S,
    EACH_UCP_SCRIPT(GEN_TABLE)
#undef GEN_TABLE
};

enum class Script : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_UCP_SCRIPT(GEN_ENUM)
#undef GEN_ENUM
};

static auto initScriptNameMap() {
  StrRefMap<Script> map;
  for (unsigned int i = 0; i < std::size(scriptNames); i++) {
    auto category = static_cast<Script>(i);
    splitByDelim(scriptNames[i], '|', [&map, category](StringRef ref, bool) {
      map.emplace(ref, category);
      return true;
    });
  }
  return map;
}

static StringRef toString(Script script, bool longName) {
  if (const auto i = toUnderlying(script); i < std::size(scriptNames)) {
    StringRef name;
    unsigned int count = 0;
    const unsigned int limit = longName ? 2 : 1;
    splitByDelim(scriptNames[i], '|', [&count, &limit, &name](StringRef ref, bool) {
      name = ref;
      return ++count < limit;
    });
    return name;
  }
  return "";
}

StringRef getScript(const int codePoint) {
  if (UnicodeUtil::isValidCodePoint(codePoint)) {
    auto script = Script::Zzzz;
    for (unsigned int i = 0; i < std::size(script_set_table_except_Zzzz); i++) {
      if (script_set_table_except_Zzzz[i].contains(codePoint)) {
        script = static_cast<Script>(i);
        break;
      }
    }
    return toString(script, true);
  }
  return "";
}

static Optional<Script> parseScript(const StringRef ref) {
  static const auto map = initScriptNameMap();
  if (const auto iter = map.find(ref); iter != map.end()) {
    return iter->second;
  }
  return {};
}

static bool getScriptSet(const Script script, BuilderOrSet out) {
  CodePointSet set;
  switch (script) {
#define GEN_CASE(E, S) case Script::E:
    EACH_UCP_SCRIPT_PRIME(GEN_CASE)
#undef GEN_CASE
    if (auto ref = script_set_table_except_Zzzz[toUnderlying(script)]; out.isBuilder) {
      out.builder->add(ref);
    } else {
      *out.set = CodePointSet::borrow(ref);
    }
    return true;
  case Script::Zzzz: {
    CodePointSetBuilder builder;
    for (auto &e : script_set_table_except_Zzzz) {
      builder.add(e);
    }
    builder.complement();
    if (auto tmp = builder.build(); out.isBuilder) {
      out.builder->add(tmp.ref());
    } else {
      *out.set = std::move(tmp);
    }
    return true;
  }
  }
  return false; // normally unreachable (for a broken script)
}

Optional<Property> parseProperty(StringRef name, StringRef value, std::string *err) {
  static const StrRefMap<Property::Name> propertyNames = {
      {"General_Category", Property::Name::General_Category},
      {"gc", Property::Name::General_Category},
      {"Script", Property::Name::Script},
      {"sc", Property::Name::Script},
      {"Script_Extensions", Property::Name::Script_Extensions},
      {"scx", Property::Name::Script_Extensions},
  };
  // resolve property name
  Property::Name prefix;
  if (name.empty()) {
    prefix = Property::Name::Lone;
  } else if (auto iter = propertyNames.find(name); iter != propertyNames.end()) {
    prefix = iter->second;
  } else {
    if (err) {
      *err += "unrecognized property name: ";
      *err += name;
    }
    return {};
  }

  // resolve property value
  switch (prefix) {
  case Property::Name::General_Category:
    if (auto ret = parseCategory(value); ret.hasValue()) {
      return Property(prefix, toUnderlying(ret.unwrap()));
    }
    break;
  case Property::Name::Script:
  case Property::Name::Script_Extensions:
    if (auto ret = parseScript(value); ret.hasValue()) {
      return Property(prefix, toUnderlying(ret.unwrap()));
    }
    break;
  case Property::Name::Lone:
    break; // TODO
  }
  if (err) {
    *err += "unrecognized property value: ";
    *err += value;
  }
  return {};
}

bool getPropertySet(const Property property, BuilderOrSet out) {
  switch (property.getName()) {
  case Property::Name::General_Category:
    return getCategorySet(static_cast<Category>(property.getValue()), out);
  case Property::Name::Script:
    return getScriptSet(static_cast<Script>(property.getValue()), out);
  case Property::Name::Script_Extensions:
  case Property::Name::Lone:
    break; // TODO
  }
  return false;
}

} // namespace arsh::ucp