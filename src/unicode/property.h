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

#ifndef ARSH_UNICODE_PROPERTY_H
#define ARSH_UNICODE_PROPERTY_H

#include "../misc/enum_util.hpp"
#include "../misc/result.hpp"
#include "../misc/string_ref.hpp"
#include "emoji_seq.hpp"
#include "set_builder.h"

#include "ucp_general_category_def.in"
#include "ucp_lone_def.in"
#include "ucp_script_def.in"

namespace arsh::ucp {

// for Unicode general category

enum class Category : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_UCP_GENERAL_CATEGORY(GEN_ENUM)
#undef GEN_ENUM
};

Optional<Category> parseCategory(StringRef ref);

Optional<Category> getCategory(int codePoint);

StringRef toString(Category category, bool longName = false);

// for Unicode Script property

enum class Script : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_UCP_SCRIPT(GEN_ENUM)
#undef GEN_ENUM
};

Optional<Script> parseScript(StringRef ref);

Optional<Script> getScript(int codePoint);

StringRef toString(Script script, bool longName = true);

enum class Lone : unsigned char {
#define GEN_ENUM(E) E,
  EACH_UCP_LONE_PROPERTY(GEN_ENUM)
#undef GEN_ENUM
};

StringRef toString(Lone lone, bool longName = true);

// Unicode Property api

#define EACH_UCP_PROPERTY_NAME(E)                                                                  \
  E(General_Category, "gc")                                                                        \
  E(Script, "sc")                                                                                  \
  E(Script_Extensions, "scx")

class Property {
public:
  enum class Name : unsigned char {
#define GEN_ENUM(E, A) E,
    EACH_UCP_PROPERTY_NAME(GEN_ENUM)
#undef GEN_ENUM
        Lone, // for lone property
  };

  static_assert(sizeof(Category) == sizeof(Name));

private:
  Name name;

  unsigned char value;

public:
  constexpr Property(Name name, unsigned char value) : name(name), value(value) {}

  static constexpr Property category(Category cate) {
    return {Name::General_Category, toUnderlying(cate)};
  }

  Name getName() const { return this->name; }

  unsigned char getValue() const { return this->value; }
};

Optional<Property> parseProperty(StringRef name, StringRef value, std::string *err);

inline Optional<Property> parseProperty(const StringRef value, std::string *err) {
  return parseProperty("", value, err);
}

struct BuilderOrSet {
  bool isBuilder;
  union {
    CodePointSetBuilder *builder;
    CodePointSet *set;
  };

  explicit BuilderOrSet(CodePointSetBuilder &builder) : isBuilder(true), builder(&builder) {}

  explicit BuilderOrSet(CodePointSet &set) : isBuilder(false), set(&set) {}
};

bool getPropertySet(Property property, BuilderOrSet out);

inline CodePointSet getPropertySet(const Property property) {
  CodePointSet set;
  getPropertySet(property, BuilderOrSet(set));
  return set;
}

/**
 * check if codePoint has prime lone property (not combined).
 * @param codePoint
 * @param lone
 * @return
 */
bool hasPrimeLoneProperty(int codePoint, Lone lone);

inline bool isExtendedPictographic(int codePoint) {
  return hasPrimeLoneProperty(codePoint, Lone::Extended_Pictographic);
}

Optional<RGIEmojiSeq> parseEmojiProperty(StringRef ref);

const char *toString(RGIEmojiSeq p);

RGIEmojiSeq getEmojiProperty(StringRef ref);

} // namespace arsh::ucp

#endif // ARSH_UNICODE_PROPERTY_H
