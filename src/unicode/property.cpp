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
#include "property.h"

namespace arsh::ucp {

#define PROPERTY_SET_RANGE BMPCodePointRange
#define PROPERTY_SET_RANGE_TABLE CodePointSetRef
#define PACKED(F, L) PackedNonBMPCodePointRange::pack(F, L)
#define PROPERTY_SET_RANGE_TABLE_OFFSET OffsetEntry

struct OffsetEntry {
  unsigned short offset;
  unsigned short bmpSize;
  unsigned short packedSize;
  unsigned short tableSize;
};

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

constexpr unsigned int categoryTableSize() {
  return std::size(category_set_table_offset_except_Cn);
}

static CodePointSetRef getCategoryTable(unsigned int index) {
  assert(index < categoryTableSize());
  const OffsetEntry &e = category_set_table_offset_except_Cn[index];
  return {e.bmpSize, e.packedSize, category_set_table_except_Cn + e.offset, e.tableSize};
}

Optional<Category> getCategory(const int codePoint) {
  if (UnicodeUtil::isValidCodePoint(codePoint)) {
    for (unsigned int i = 0; i < categoryTableSize(); i++) {
      if (getCategoryTable(i).contains(codePoint)) {
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
    if (auto ref = getCategoryTable(toUnderlying(category)); out.isBuilder) {
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
      if (unsigned int index = toUnderlying(c); index < categoryTableSize()) {
        b->add(getCategoryTable(index));
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
    for (unsigned int i = 0; i < categoryTableSize(); i++) {
      builder.add(getCategoryTable(i));
    }
    builder.complement();

    if (category == Category::C) {
      for (auto c : {Category::Cc, Category::Cf, Category::Cs, Category::Co}) {
        builder.add(getCategoryTable(toUnderlying(c)));
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

StringRef toString(Script script, bool longName) {
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

constexpr unsigned int scriptTableSize() { return std::size(script_set_table_offset_except_Zzzz); }

static CodePointSetRef getScriptTable(unsigned int index) {
  assert(index < scriptTableSize());
  const OffsetEntry &e = script_set_table_offset_except_Zzzz[index];
  return {e.bmpSize, e.packedSize, script_set_table_except_Zzzz + e.offset, e.tableSize};
}

Optional<Script> getScript(const int codePoint) {
  if (UnicodeUtil::isValidCodePoint(codePoint)) {
    for (unsigned int i = 0; i < scriptTableSize(); i++) {
      if (getScriptTable(i).contains(codePoint)) {
        return static_cast<Script>(i);
      }
    }
    return Script::Zzzz;
  }
  return {};
}

Optional<Script> parseScript(const StringRef ref) {
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
    if (auto ref = getScriptTable(toUnderlying(script)); out.isBuilder) {
      out.builder->add(ref);
    } else {
      *out.set = CodePointSet::borrow(ref);
    }
    return true;
  case Script::Zzzz: {
    CodePointSetBuilder builder;
    for (unsigned int i = 0; i < scriptTableSize(); i++) {
      builder.add(getScriptTable(i));
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

static bool getScriptXSet(const Script script, BuilderOrSet out) {
  CodePointSetRef ref;
  switch (script) {
#define GEN_CASE(E, O, B, P, S)                                                                    \
  case Script::E:                                                                                  \
    ref = {B, P, scriptx_set_table + (O), S};                                                      \
    break;
    EACH_UCP_SCRIPT_EXTENSION(GEN_CASE)
#undef GEN_CASE
  default:
    break;
  }
  if (ref.getSize()) {
    if (out.isBuilder) {
      out.builder->add(ref);
    } else {
      *out.set = CodePointSet::borrow(ref);
    }
    return true;
  }
  return false;
}

// ===========================
// ==     Lone Property     ==
// ===========================

#include "ucp_lone.in"
#include "ucp_lone_def.in"

enum class Lone : unsigned char {
#define GEN_ENUM(E) E,
  EACH_UCP_LONE_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
};

static constexpr const char *loneNames[] = {
#define GEN_TABLE(E, S) S,
    EACH_UCP_LONE_PROPERTY_NAME(GEN_TABLE)
#undef GEN_TABLE
};

static auto initLoneNameMap() {
  StrRefMap<Lone> map;
  for (unsigned int i = UCP_LONE_PROPERTY_PRIME_INTERNAL_SIZE; i < UCP_LONE_PROPERTY_SIZE; i++) {
    auto category = static_cast<Lone>(i);
    const unsigned int index = i - UCP_LONE_PROPERTY_PRIME_INTERNAL_SIZE;
    splitByDelim(loneNames[index], '|', [&map, category](StringRef ref, bool) {
      map.emplace(ref, category);
      return true;
    });
  }
  return map;
}

static CodePointSetRef getLonePrimeTable(unsigned int index) {
  assert(index < std::size(lone_set_prime_table_offset));
  const OffsetEntry &e = lone_set_prime_table_offset[index];
  return {e.bmpSize, e.packedSize, lone_set_prime_table + e.offset, e.tableSize};
}

static Optional<Lone> parseLone(const StringRef ref) {
  static const auto map = initLoneNameMap();
  if (const auto iter = map.find(ref); iter != map.end()) {
    return iter->second;
  }
  return {};
}

constexpr Property fromLone(Lone l) { return {Property::Name::Lone, toUnderlying(l)}; }

static bool getLoneSet(const Lone lone, BuilderOrSet out) {
  switch (lone) {
#define GEN_CASE(E) case Lone::E:
    EACH_UCP_LONE_PROPERTY_PRIME_INTERNAL(GEN_CASE)
    EACH_UCP_LONE_PROPERTY_PRIME(GEN_CASE)
#undef GEN_CASE
    if (auto ref = getLonePrimeTable(toUnderlying(lone)); out.isBuilder) {
      out.builder->add(ref);
    } else {
      *out.set = CodePointSet::borrow(ref);
    }
    return true;
  case Lone::Alphabetic: {
    // Generated from: Lowercase + Uppercase + Lt + Lm + Lo + Nl + Other_Alphabetic
    CodePointSetBuilder builder;
    constexpr Property combs[] = {
        fromLone(Lone::Lowercase),        fromLone(Lone::Uppercase),
        Property::category(Category::Lt), Property::category(Category::Lm),
        Property::category(Category::Lo), Property::category(Category::Nl),
        fromLone(Lone::Other_Alphabetic),
    };
    for (auto &p : combs) {
      getPropertySet(p, BuilderOrSet(builder));
    }
    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  case Lone::Assigned: {
    /*
     * \P{Cn}
     * --> !(Cn)
     */
    CodePointSetBuilder builder;
    getPropertySet(Property::category(Category::Cn), BuilderOrSet(builder));
    builder.complement();
    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  case Lone::Bidi_Mirrored:                // TODO:
  case Lone::Changes_When_NFKC_Casefolded: // TODO:
    break;
  case Lone::Default_Ignorable_Code_Point: {
    /*
     * Generated from:
     *    Other_Default_Ignorable_Code_Point + Cf (Format characters)
     *    + Variation_Selector - White_Space - FFF9..FFFB (Interlinear annotation format characters)
     *    - 13430..1343F (Egyptian hieroglyph format characters) - Prepended_Concatenation_Mark
     * (Exceptional format characters that should be visible)
     */
    CodePointSetBuilder builder;
    constexpr Property combs[] = {
        fromLone(Lone::Other_Default_Ignorable_Code_Point),
        Property::category(Category::Cf),
        fromLone(Lone::Variation_Selector),
    };
    for (auto &p : combs) {
      getPropertySet(p, BuilderOrSet(builder));
    }
    builder.sub(getPropertySet(fromLone(Lone::White_Space)).ref());
    {
      CodePointSetBuilder tmp;
      tmp.add(0xFFF9, 0xFFFB);
      builder.sub(tmp.build().ref());
      tmp.clear();
      tmp.add(0x13430, 0x1343F);
      builder.sub(tmp.build().ref());
    }
    builder.sub(getPropertySet(fromLone(Lone::Prepended_Concatenation_Mark)).ref());

    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  case Lone::Grapheme_Base: {
    /**
     * Generated from: [0..10FFFF] - Cc - Cf - Cs - Co - Cn - Zl - Zp - Grapheme_Extend
     * --> !(Cc + Cf + Cs + Co + Cn + Zl + Zp + Grapheme_Extend)
     * --> !(C + Zl + Zp + Grapheme_Extend)
     */
    CodePointSetBuilder builder;
    constexpr Property combs[] = {
        Property::category(Category::C),
        Property::category(Category::Zl),
        Property::category(Category::Zp),
        fromLone(Lone::Grapheme_Extend),
    };
    for (auto &p : combs) {
      getPropertySet(p, BuilderOrSet(builder));
    }
    builder.complement();
    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  case Lone::Grapheme_Extend: {
    // Generated from: Me + Mn + Other_Grapheme_Extend
    CodePointSetBuilder builder;
    constexpr Property combs[] = {
        Property::category(Category::Me),
        Property::category(Category::Mn),
        fromLone(Lone::Other_Grapheme_Extend),
    };
    for (auto &p : combs) {
      getPropertySet(p, BuilderOrSet(builder));
    }
    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  case Lone::Lowercase: {
    // Generated from: Ll + Other_Lowercase
    CodePointSetBuilder builder;
    getPropertySet(Property::category(Category::Ll), BuilderOrSet(builder));
    getPropertySet(fromLone(Lone::Other_Lowercase), BuilderOrSet(builder));
    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  case Lone::Math: {
    // Generated from: Sm + Other_Math
    CodePointSetBuilder builder;
    getPropertySet(Property::category(Category::Sm), BuilderOrSet(builder));
    getPropertySet(fromLone(Lone::Other_Math), BuilderOrSet(builder));
    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  case Lone::Uppercase: {
    // Generated from: Lu + Other_Uppercase
    CodePointSetBuilder builder;
    getPropertySet(Property::category(Category::Lu), BuilderOrSet(builder));
    getPropertySet(fromLone(Lone::Other_Uppercase), BuilderOrSet(builder));
    if (out.isBuilder) {
      out.builder->add(builder);
    } else {
      *out.set = builder.build();
    }
    return true;
  }
  }
  return false; // normally unreachable (for a broken lone property)
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
    prefix = Property::Name::Lone; // Lone or General Category
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
      return Property::category(ret.unwrap());
    }
    break;
  case Property::Name::Script:
  case Property::Name::Script_Extensions:
    if (auto ret = parseScript(value); ret.hasValue()) {
      return Property(prefix, toUnderlying(ret.unwrap()));
    }
    break;
  case Property::Name::Lone:
    if (auto ret = parseCategory(value); ret.hasValue()) {
      return Property::category(ret.unwrap());
    }
    if (auto ret = parseLone(value); ret.hasValue()) {
      return fromLone(ret.unwrap());
    }
    break;
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
    return getScriptXSet(static_cast<Script>(property.getValue()), out);
  case Property::Name::Lone:
    return getLoneSet(static_cast<Lone>(property.getValue()), out);
  }
  return false;
}

bool isExtendedPictographic(const int codePoint) {
  auto set =
      getPropertySet(Property(Property::Name::Lone, toUnderlying(Lone::Extended_Pictographic)));
  return set.ref().contains(codePoint);
}

} // namespace arsh::ucp