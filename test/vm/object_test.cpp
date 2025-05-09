#include "gtest/gtest.h"

#include <cmath>

#include <misc/split_random.hpp>
#include <object.h>
#include <object_util.h>
#include <ordered_map.h>
#include <type_pool.h>

using namespace arsh;

struct ObjectTest : ::testing::Test {
  struct Param {
    StringRef ref;
    bool smallStr;
    unsigned int meta;
  };

  static void checkMetaData(const Param &p) {
    auto v = Value::createStr(p.ref);

    ASSERT_EQ(p.ref, v.asStrRef());
    ASSERT_EQ(p.ref.size(), v.asStrRef().size());
    ASSERT_EQ(p.smallStr, isSmallStr(v.kind()));

    auto o = v.withMetaData(p.meta);
    ASSERT_TRUE(Equality()(v, o));
    ASSERT_EQ(p.ref, o.asStrRef());
    ASSERT_EQ(p.ref.size(), o.asStrRef().size());
    ASSERT_EQ(p.meta, o.getMetaData());
  }
};

static unsigned int next32(L64X128MixRNG &rng) {
  uint64_t v = rng.next();
  v &= UINT32_MAX;
  return static_cast<unsigned int>(v);
}

TEST_F(ObjectTest, meta) {
  L64X128MixRNG rng(42);
  ASSERT_NE(next32(rng), next32(rng));

  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abcdef", false, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abcde", false, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abcd", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789abc", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789ab", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789a", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456789", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"012345678", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"01234567", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123456", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"012345", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"01234", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0123", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"012", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"01", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"0", true, next32(rng)}));
  ASSERT_NO_FATAL_FAILURE(checkMetaData({"", true, next32(rng)}));
}

TEST(MapTest, base) {
  TypePool pool;
  const auto &mapType = *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::Int)).take();

  auto value = Value::create<OrderedMapObject>(mapType, 42);
  auto obj = toObjPtr<OrderedMapObject>(value);

  ASSERT_EQ(0, obj->size());

  auto pair = obj->insert(Value::createStr("ABC"), Value::createInt(12));
  ASSERT_EQ(1, obj->size());
  ASSERT_EQ(1, obj->getEntries().getUsedSize());
  int retIndex = obj->lookup(Value::createStr("ABCD"));
  ASSERT_EQ(-1, retIndex);
  retIndex = obj->lookup(Value::createStr("ABC"));
  ASSERT_EQ(0, retIndex);
  ASSERT_EQ(retIndex, pair.first);
  ASSERT_EQ(OrderedMapObject::InsertStatus::OK, pair.second);
  ASSERT_EQ("ABC", (*obj)[retIndex].getKey().asStrRef());
  ASSERT_EQ(12, (*obj)[retIndex].getValue().asInt());

  // insert already defined key
  pair = obj->insert(Value::createStr("ABC"), Value::createInt(1232));
  ASSERT_EQ(1, obj->size());
  ASSERT_EQ(1, obj->getEntries().getUsedSize());
  ASSERT_EQ(0, pair.first);
  ASSERT_EQ(OrderedMapObject::InsertStatus::NOP, pair.second);

  pair = obj->insert(Value::createStr("1234"), Value::createInt(-99));
  ASSERT_EQ(2, obj->size());
  ASSERT_EQ(2, obj->getEntries().getUsedSize());
  ASSERT_EQ(1, pair.first);
  ASSERT_EQ(OrderedMapObject::InsertStatus::OK, pair.second);
  retIndex = obj->lookup(Value::createStr("1234"));
  ASSERT_EQ(1, retIndex);
  ASSERT_EQ("1234", (*obj)[retIndex].getKey().asStrRef());
  ASSERT_EQ(-99, (*obj)[retIndex].getValue().asInt());

  pair = obj->insert(Value::createStr("***"), Value::createInt(9876));
  ASSERT_EQ(3, obj->size());
  ASSERT_EQ(3, obj->getEntries().getUsedSize());
  ASSERT_EQ(2, pair.first);
  ASSERT_EQ(OrderedMapObject::InsertStatus::OK, pair.second);
  retIndex = obj->lookup(Value::createStr("***"));
  ASSERT_EQ(2, retIndex);
  ASSERT_EQ("***", (*obj)[retIndex].getKey().asStrRef());
  ASSERT_EQ(9876, (*obj)[retIndex].getValue().asInt());
  ASSERT_EQ("1234", (*obj)[1].getKey().asStrRef());
  ASSERT_EQ(-99, (*obj)[1].getValue().asInt());
  ASSERT_EQ("ABC", (*obj)[0].getKey().asStrRef());
  ASSERT_EQ(12, (*obj)[0].getValue().asInt());
}

static std::string location(unsigned int index,
                            const std::vector<std::pair<std::string, uint64_t>> &values) {
  auto &keyValue = values[index];

  std::string message = "at ";
  message += std::to_string(index);
  message += " (";
  message += keyValue.first;
  message += ", ";
  message += std::to_string(keyValue.second);
  message += ")";
  return message;
}

TEST(MapTest, rand1) {
  TypePool pool;
  const auto &mapType = *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::Int)).take();

  auto value = Value::create<OrderedMapObject>(mapType, 42);
  auto obj = toObjPtr<OrderedMapObject>(value);

  ASSERT_EQ(0, obj->size());

  constexpr unsigned int N = 2000;
  L64X128MixRNG rng(42);
  std::vector<std::pair<std::string, uint64_t>> keyValues;
  keyValues.reserve(N);
  for (unsigned int i = 0; i < N; i++) {
    static_assert(sizeof(uint64_t) == sizeof(uintmax_t));

    uint64_t v = rng.next();
    char data[64];
    int size = snprintf(data, std::size(data), "%#jx", static_cast<uintmax_t>(v));
    keyValues.emplace_back(std::string(data, size), v);
  }

  // insert
  ASSERT_FALSE(keyValues.empty());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    const auto &keyValue = keyValues[i];
    auto pair = obj->insert(Value::createStr(keyValue.first),
                            Value::createInt(static_cast<int64_t>(keyValue.second)));
    ASSERT_EQ(OrderedMapObject::InsertStatus::OK, pair.second);
    ASSERT_EQ(i, pair.first);
    ASSERT_EQ(keyValue.first, (*obj)[pair.first].getKey().asStrRef());
    ASSERT_EQ(keyValue.second, (*obj)[pair.first].getValue().asInt());
    ASSERT_EQ(i + 1, obj->size());
    ASSERT_EQ(i + 1, obj->getEntries().getUsedSize());
  }

  // lookup
  ASSERT_FALSE(keyValues.empty());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    const auto &keyValue = keyValues[i];
    auto retIndex = obj->lookup(Value::createStr(keyValue.first));
    ASSERT_EQ(i, retIndex);
    ASSERT_EQ(keyValue.first, (*obj)[retIndex].getKey().asStrRef());
    ASSERT_EQ(keyValue.second, (*obj)[retIndex].getValue().asInt());
  }

  // already inserted
  ASSERT_FALSE(keyValues.empty());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    const auto &keyValue = keyValues[i];
    auto pair = obj->insert(Value::createStr(keyValue.first),
                            Value::createInt(static_cast<int64_t>(keyValue.second + 9999)));
    ASSERT_EQ(OrderedMapObject::InsertStatus::NOP, pair.second);
    ASSERT_EQ(i, pair.first);
    ASSERT_EQ(keyValue.first, (*obj)[pair.first].getKey().asStrRef());
    ASSERT_EQ(keyValue.second, (*obj)[pair.first].getValue().asInt());
    ASSERT_EQ(keyValues.size(), obj->size());
    ASSERT_EQ(keyValues.size(), obj->getEntries().getUsedSize());
  }

  // lookup not found key
  for (unsigned int i = 0; i < 150; i++) {
    uint64_t v = rng.next();
    char data[64];
    int size = snprintf(data, std::size(data), "%#jx", static_cast<uintmax_t>(v));
    std::string key(data, size);

    SCOPED_TRACE("at " + std::to_string(i) + " " + key);

    auto retIndex = obj->lookup(Value::createStr(key));
    ASSERT_EQ(-1, retIndex);
  }

  // check insertion order
  std::vector<std::pair<StringRef, uint64_t>> entries;
  for (auto &e : obj->getEntries()) {
    if (!e) {
      continue;
    }
    entries.emplace_back(e.getKey().asStrRef(), static_cast<uint64_t>(e.getValue().asInt()));
  }
  ASSERT_EQ(entries.size(), keyValues.size());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    ASSERT_EQ(keyValues[i].first, entries[i].first);
    ASSERT_EQ(keyValues[i].first, entries[i].first);
  }

  // remove
  std::unordered_set<std::string> removeTargets;
  for (unsigned int i = 0; i < 777; i++) {
    auto index = static_cast<unsigned int>(rng.next() % N);
    removeTargets.insert(keyValues[index].first);
  }

  for (auto iter = keyValues.begin(); iter != keyValues.end();) {
    const auto &e = *iter;
    if (removeTargets.find(e.first) != removeTargets.end()) {
      SCOPED_TRACE("(" + e.first + ", " + std::to_string(e.second) + ")");

      auto entry = obj->remove(Value::createStr(e.first));
      ASSERT_TRUE(entry);
      ASSERT_EQ(e.first, entry.getKey().asStrRef());
      ASSERT_EQ(e.second, entry.getValue().asInt());
      iter = keyValues.erase(iter);
    } else {
      ++iter;
    }
  }
  ASSERT_EQ(keyValues.size(), obj->size());

  // lookup removed entry
  for (auto &e : removeTargets) {
    SCOPED_TRACE(e);

    auto retIndex = obj->lookup(Value::createStr(e));
    ASSERT_EQ(-1, retIndex);
  }
  // lookup remain entry
  for (auto &e : keyValues) {
    SCOPED_TRACE("(" + e.first + ", " + std::to_string(e.second) + ")");

    auto retIndex = obj->lookup(Value::createStr(e.first));
    ASSERT_NE(-1, retIndex);
    ASSERT_EQ(e.first, (*obj)[retIndex].getKey().asStrRef());
    ASSERT_EQ(e.second, (*obj)[retIndex].getValue().asInt());
  }

  // check insertion order after remove
  entries.clear();
  for (auto &e : obj->getEntries()) {
    if (!e) {
      continue;
    }
    entries.emplace_back(e.getKey().asStrRef(), static_cast<uint64_t>(e.getValue().asInt()));
  }
  ASSERT_EQ(entries.size(), keyValues.size());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    ASSERT_EQ(keyValues[i].first, entries[i].first);
    ASSERT_EQ(keyValues[i].first, entries[i].first);
  }

  // insert after remove
  for (unsigned int i = 0; i < 1000; i++) {
    uint64_t v = rng.next();
    char data[64];
    int size = snprintf(data, std::size(data), "%#jX", static_cast<uintmax_t>(v));
    std::string key(data, size);
    keyValues.emplace_back(key, v);

    SCOPED_TRACE("(" + key + ", " + std::to_string(v) + ")");

    auto pair = obj->insert(Value::createStr(key), Value::createInt(static_cast<int64_t>(v)));
    ASSERT_EQ(OrderedMapObject::InsertStatus::OK, pair.second);
    ASSERT_NE(-1, pair.first);

    ASSERT_EQ(key, (*obj)[pair.first].getKey().asStrRef());
    ASSERT_EQ(v, (*obj)[pair.first].getValue().asInt());
  }

  // check insertion order after remove and insertion
  entries.clear();
  for (auto &e : obj->getEntries()) {
    if (!e) {
      continue;
    }
    entries.emplace_back(e.getKey().asStrRef(), static_cast<uint64_t>(e.getValue().asInt()));
  }
  ASSERT_EQ(entries.size(), keyValues.size());
  for (unsigned int i = 0; i < keyValues.size(); i++) {
    SCOPED_TRACE(location(i, keyValues));

    ASSERT_EQ(keyValues[i].first, entries[i].first);
    ASSERT_EQ(keyValues[i].first, entries[i].first);
  }
}

TEST(MapTest, rand2) {
  TypePool pool;
  const auto &mapType = *pool.createMapType(pool.get(TYPE::String), pool.get(TYPE::Int)).take();

  auto value = Value::create<OrderedMapObject>(mapType, 42);
  auto obj = toObjPtr<OrderedMapObject>(value); // FIXME:
}

class ObjectUtilTest : public ::testing::Test {
protected:
  TypePool pool;

public:
  struct ArrayBuilder {
    ObjPtr<ArrayObject> obj;

    ArrayBuilder &add(Value &&v) {
      this->obj->append(std::move(v));
      return *this;
    }
  };

  ArrayBuilder array(TYPE t) { return this->array(this->pool.get(t)); }

  ArrayBuilder array(const Type &type) {
    auto ret = this->pool.createArrayType(type);
    auto obj = ret ? toObjPtr<ArrayObject>(Value::create<ArrayObject>(*ret.asOk())) : nullptr;
    return ArrayBuilder{.obj = std::move(obj)};
  }

  struct MapBuilder {
    ObjPtr<OrderedMapObject> obj;

    MapBuilder &add(Value &&k, Value &&v) {
      this->obj->insert(k, std::move(v));
      return *this;
    }
  };

  MapBuilder map(TYPE k, TYPE v) { return this->map(this->pool.get(k), this->pool.get(v)); }

  MapBuilder map(const Type &k, const Type &v) {
    auto ret = this->pool.createMapType(k, v);
    auto obj = ret ? toObjPtr<OrderedMapObject>(Value::create<OrderedMapObject>(*ret.asOk(), 42))
                   : nullptr;
    return MapBuilder{.obj = std::move(obj)};
  }

  template <typename T, typename... Res>
  ObjPtr<BaseObject> tuple(T &&t, Res &&...res) {
    std::vector<Value> values = {t, std::forward<Res>(res)...};
    std::vector<const Type *> types;
    for (auto &e : values) {
      types.push_back(&this->pool.get(e.getTypeID()));
    }
    if (auto ret = this->pool.createTupleType(std::move(types))) {
      auto obj = toObjPtr<BaseObject>(Value::create<BaseObject>(cast<TupleType>(*ret.asOk())));
      const unsigned int size = obj->getFieldSize();
      for (unsigned int i = 0; i < size; i++) {
        (*obj)[i] = std::move(values[i]);
      }
      return obj;
    }
    return nullptr;
  }

  void checkStr(StringRef expect, const Value &value) const {
    StrAppender appender(SYS_LIMIT_STRING_MAX);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsStr(value));
    ASSERT_FALSE(stringifier.hasOverflow());
    auto actual = std::move(appender).take();
    ASSERT_EQ(expect, actual);
  }

  void checkInterp(StringRef expect, const Value &value) const {
    StrAppender appender(SYS_LIMIT_STRING_MAX);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsInterp(value));
    ASSERT_FALSE(stringifier.hasOverflow());
    auto actual = std::move(appender).take();
    ASSERT_EQ(expect, actual);
  }
};

TEST_F(ObjectUtilTest, base) {
  ASSERT_TRUE(Equality()(Value::createInvalid(), Value::createInvalid()));
  ASSERT_TRUE(Equality()(Value::createInt(1234), Value::createInt(1234)));
  ASSERT_FALSE(Equality()(Value::createInt(1234), Value::createInt(1233)));
  ASSERT_TRUE(Equality()(Value::createBool(true), Value::createBool(true)));
  ASSERT_TRUE(Equality()(Value::createBool(false), Value::createBool(false)));
  ASSERT_FALSE(Equality()(Value::createBool(true), Value::createBool(false)));
  ASSERT_TRUE(Equality()(Value::createSig(SIGINT), Value::createSig(SIGINT)));
  ASSERT_TRUE(Equality()(Value::createFloat(3.14), Value::createFloat(3.14)));
  ASSERT_TRUE(Equality(true)(Value::createFloat(3.14), Value::createFloat(3.14)));
  ASSERT_TRUE(Equality()(Value::createFloat(0.0 / 0.0), Value::createFloat(0.0 / 0.0)));
  ASSERT_FALSE(Equality(true)(Value::createFloat(0.0 / 0.0), Value::createFloat(0.0 / 0.0)));
  ASSERT_TRUE(Equality()(Value::createStr(""), Value::createStr("")));
  ASSERT_FALSE(Equality()(Value::createStr("aa"), Value::createStr("")));
  ASSERT_FALSE(Equality()(Value::create<UnixFdObject>(234), Value::create<UnixFdObject>(234)));

  ASSERT_TRUE(Ordering()(Value::createInvalid(), Value::createInvalid()) == 0);
  ASSERT_TRUE(Ordering()(Value::createBool(false), Value::createBool(true)) < 0);
  ASSERT_TRUE(Ordering()(Value::createBool(true), Value::createBool(false)) > 0);
  ASSERT_TRUE(Ordering()(Value::createBool(true), Value::createBool(true)) == 0);
  ASSERT_TRUE(Ordering()(Value::createBool(false), Value::createBool(false)) == 0);
  ASSERT_TRUE(Ordering()(Value::createSig(SIGINT), Value::createSig(SIGCHLD)) < 0);
  ASSERT_TRUE(Ordering()(Value::createSig(SIGUSR2), Value::createSig(SIGHUP)) > 0);
  ASSERT_TRUE(Ordering()(Value::createSig(SIGINT), Value::createSig(SIGINT)) == 0);
  ASSERT_TRUE(Ordering()(Value::createInt(-1234), Value::createInt(0)) < 0);
  ASSERT_TRUE(Ordering()(Value::createInt(-1234), Value::createInt(-9999)) > 0);
  ASSERT_TRUE(Ordering()(Value::createInt(12), Value::createInt(12)) == 0);
  ASSERT_TRUE(Ordering()(Value::createFloat(std::nan("")), Value::createFloat(std::nan(""))) == 0);
  ASSERT_TRUE(Ordering()(Value::createFloat(std::nan("")), Value::createFloat(std::nan("0xF"))) ==
              0);
  ASSERT_TRUE(Ordering()(Value::createFloat(std::nan("")), Value::createFloat(-9.0 / 0.0)) > 0);
  ASSERT_TRUE(Ordering()(Value::createFloat(std::nan("")), Value::createFloat(9.0 / 0.0)) > 0);
  ASSERT_TRUE(Ordering()(Value::createFloat(8.0 / 0.0), Value::createFloat(-345.0 / 0.0)) > 0);
  ASSERT_TRUE(Ordering()(Value::createStr(""), Value::createStr("")) == 0);
  ASSERT_TRUE(Ordering()(Value::createStr(""), Value::createStr("1")) < 0);
  ASSERT_TRUE(Ordering()(Value::createStr("112"), Value::createStr("11")) > 0);
  ASSERT_TRUE(Ordering()(Value::createStr("1234"), Value::createStr("1234")) == 0);
  ASSERT_TRUE(Ordering()(Value::create<UnixFdObject>(23), Value::create<UnixFdObject>(23)) != 0);
}

TEST_F(ObjectUtilTest, different) {
  ASSERT_FALSE(Equality()(Value::createInvalid(), Value::createInt(1234)));
  ASSERT_FALSE(Equality()(Value::createFloat(0.0), Value::createInt(1234)));
  ASSERT_FALSE(Equality()(Value::create<UnixFdObject>(234), Value::createStr("1234")));
  ASSERT_FALSE(Equality()(this->array(TYPE::Int).obj, this->map(TYPE::Int, TYPE::Int).obj));

  ASSERT_TRUE(Ordering()(Value::createInvalid(), Value::createBool(false)) < 0);
  ASSERT_TRUE(Ordering()(Value::createInt(1234), Value::createInvalid()) > 0);
  ASSERT_TRUE(Ordering()(Value::createInt(1234), Value::createFloat(1234)) < 0);
  ASSERT_TRUE(Ordering()(Value::createFloat(1234), Value::createInt(1234)) > 0);
  ASSERT_TRUE(Ordering()(Value::create<UnixFdObject>(234), Value::createStr("1234")) < 0);
  ASSERT_TRUE(Ordering()(Value::createStr("1234"), Value::create<UnixFdObject>(234)) > 0);
  ASSERT_TRUE(Ordering()(this->array(TYPE::Int).obj, this->map(TYPE::Int, TYPE::Int).obj) < 0);
  ASSERT_TRUE(Ordering()(this->map(TYPE::Int, TYPE::Int).obj, this->array(TYPE::Int).obj) > 0);
}

TEST_F(ObjectUtilTest, array1) {
  auto obj1 = this->array(TYPE::String).obj;
  auto obj2 = this->array(TYPE::String).obj;
  ASSERT_TRUE(Equality()(obj1, obj2));
  obj2 = this->array(TYPE::Int).obj;
  ASSERT_TRUE(Equality()(obj1, obj2)); // true, even if different type
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);

  obj1 = this->array(TYPE::Int).add(Value::createInt(12)).add(Value::createInt(34)).obj;
  obj2 = this->array(TYPE::Int).add(Value::createInt(12)).add(Value::createInt(34)).obj;
  ASSERT_TRUE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);

  obj2->refValues().pop_back();
  obj2->append(Value::createInt(56));
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) < 0);

  obj1->refValues()[0] = Value::createInt(999);
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) > 0);

  obj1 = this->array(TYPE::Int).add(Value::createInt(12)).add(Value::createInt(34)).obj;
  obj2 = this->array(TYPE::Int)
             .add(Value::createInt(12))
             .add(Value::createInt(34))
             .add(Value::createInt(0))
             .obj;
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) < 0);
  ASSERT_FALSE(Equality()(obj2, obj1));
  ASSERT_TRUE(Ordering()(obj2, obj1) > 0);
}

TEST_F(ObjectUtilTest, array2) {
  auto obj1 = this->array(TYPE::Any).obj;
  obj1->append(obj1);
  auto obj2 = this->array(TYPE::Any).add(Value::createInt(234)).obj;
  Equality equality;
  Ordering ordering;
  ASSERT_FALSE(equality.hasOverflow());
  ASSERT_FALSE(ordering.hasOverflow());
  ASSERT_TRUE(equality(obj2, obj2));
  ASSERT_FALSE(equality.hasOverflow());

  ASSERT_FALSE(equality(obj1, obj2));
  ASSERT_FALSE(equality.hasOverflow());
  ASSERT_TRUE(equality(obj1, obj1));
  ASSERT_FALSE(equality.hasOverflow());
  ASSERT_TRUE(ordering(obj1, obj1) == 0);
  ASSERT_FALSE(ordering.hasOverflow());

  obj2 = this->array(TYPE::Any).obj;
  obj2->append(obj2);
  ASSERT_FALSE(equality(obj1, obj2));
  ASSERT_TRUE(equality.hasOverflow());
  ASSERT_TRUE(ordering(obj1, obj2) < 0);
  ASSERT_TRUE(ordering.hasOverflow());
  ASSERT_TRUE(ordering(obj2, obj1) < 0);
  ASSERT_TRUE(ordering.hasOverflow());

  // clear
  obj1->refValues().clear();
  obj2->refValues().clear();
}

TEST_F(ObjectUtilTest, tuple1) {
  auto obj1 = this->tuple(Value::createInt(12), Value::createFloat(std::nan("")));
  auto obj2 = this->tuple(Value::createInt(12), Value::createFloat(std::nan("")));
  ASSERT_TRUE(Equality()(obj1, obj2)); // total order
  ASSERT_FALSE(Equality(true)(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);

  obj1 = this->tuple(Value::createInt(12), Value::createStr("aaa"), Value::createBool(true));
  obj2 = this->tuple(Value::createInt(12), Value::createStr("aaa"));
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) > 0);
  ASSERT_TRUE(Ordering()(obj2, obj1) < 0);
}

TEST_F(ObjectUtilTest, tuple2) {
  auto obj1 = this->tuple(Value::createInt(12), Value::createFloat(std::nan("")));
  auto obj2 = this->tuple(Value::createInt(12), Value::createFloat(std::nan("")));
  (*obj1)[1] = obj2;
  (*obj2)[1] = obj1;

  Equality equality;
  Ordering ordering;
  ASSERT_TRUE(equality(obj1, obj1));
  ASSERT_FALSE(equality.hasOverflow());
  ASSERT_TRUE(ordering(obj1, obj1) == 0);
  ASSERT_FALSE(ordering.hasOverflow());

  ASSERT_FALSE(equality(obj1, obj2));
  ASSERT_TRUE(equality.hasOverflow());
  ASSERT_TRUE(ordering(obj1, obj2) < 0);
  ASSERT_TRUE(ordering.hasOverflow());
  ASSERT_TRUE(ordering(obj2, obj1) < 0);
  ASSERT_TRUE(ordering.hasOverflow());

  // clear
  (*obj1)[1] = Value::createInvalid();
  (*obj2)[1] = Value::createInvalid();
}

TEST_F(ObjectUtilTest, map1) {
  auto obj1 = this->map(TYPE::String, TYPE::Int).obj;
  auto obj2 = this->map(TYPE::String, TYPE::Int).obj;
  ASSERT_TRUE(Equality()(obj1, obj1));
  ASSERT_TRUE(Ordering()(obj1, obj1) == 0);
  ASSERT_TRUE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);
  obj2 = this->map(TYPE::Int, TYPE::Int).obj;
  ASSERT_TRUE(Equality()(obj1, obj2)); // true, even if different type
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);

  obj1 = this->map(TYPE::String, TYPE::Float)
             .add(Value::createStr("AAA"), Value::createFloat(std::nan("")))
             .obj;
  obj2 = this->map(TYPE::String, TYPE::Float)
             .add(Value::createStr("AAA"), Value::createFloat(std::nan("")))
             .obj;
  ASSERT_TRUE(Equality()(obj1, obj2));
  ASSERT_FALSE(Equality(true)(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);

  obj1 = this->map(TYPE::String, TYPE::Int)
             .add(Value::createStr("aa"), Value::createInt(2))
             .add(Value::createStr("bb"), Value::createInt(-1))
             .obj;
  obj2 = this->map(TYPE::String, TYPE::Int)
             .add(Value::createStr("aa"), Value::createInt(2))
             .add(Value::createStr("bb"), Value::createInt(-1))
             .obj;
  ASSERT_TRUE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);

  obj2 = this->map(TYPE::String, TYPE::Int)
             .add(Value::createStr("aa"), Value::createInt(2))
             .add(Value::createStr("bbb"), Value::createInt(-1))
             .obj;
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) < 0);
  ASSERT_TRUE(Ordering()(obj2, obj1) > 0);

  obj2 = this->map(TYPE::String, TYPE::Int)
             .add(Value::createStr("aa"), Value::createInt(2))
             .add(Value::createStr("bb"), Value::createInt(-134))
             .obj;
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) > 0);
  ASSERT_TRUE(Ordering()(obj2, obj1) < 0);

  obj2 = this->map(TYPE::String, TYPE::Int)
             .add(Value::createStr("aa"), Value::createInt(2))
             .add(Value::createStr("bb"), Value::createInt(-1))
             .add(Value::createStr("ccc"), Value::createInt(5))
             .obj;
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) < 0);
  ASSERT_TRUE(Ordering()(obj2, obj1) > 0);

  // key-value order independent (same entries, but different insertion order)
  obj1 = this->map(TYPE::String, TYPE::Int)
             .add(Value::createStr("aa"), Value::createInt(2))
             .add(Value::createStr("bb"), Value::createInt(-1))
             .obj;
  obj2 = this->map(TYPE::String, TYPE::Int)
             .add(Value::createStr("bb"), Value::createInt(-1))
             .add(Value::createStr("aa"), Value::createInt(2))
             .obj;
  ASSERT_TRUE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) < 0);
  ASSERT_TRUE(Ordering()(obj2, obj1) > 0);
}

TEST_F(ObjectUtilTest, map2) {
  auto obj1 = this->map(TYPE::Int, TYPE::Any).add(Value::createInt(42), Value::createInt(34)).obj;
  auto obj2 = this->map(TYPE::Int, TYPE::Any).add(Value::createInt(42), Value::createInt(34)).obj;

  Equality equality;
  Ordering ordering;
  obj1->insert(Value::createInt(99), obj2);
  obj2->insert(Value::createInt(99), obj1);
  ASSERT_TRUE(equality(obj2, obj2));
  ASSERT_TRUE(ordering(obj2, obj2) == 0);
  ASSERT_FALSE(equality.hasOverflow());
  ASSERT_FALSE(ordering.hasOverflow());

  ASSERT_FALSE(equality(obj1, obj2));
  ASSERT_TRUE(ordering(obj1, obj2) < 0);
  ASSERT_TRUE(equality.hasOverflow());
  ASSERT_TRUE(ordering.hasOverflow());

  ASSERT_FALSE(equality(obj1, obj2));
  ASSERT_TRUE(ordering(obj2, obj1) < 0);
  ASSERT_TRUE(equality.hasOverflow());
  ASSERT_TRUE(ordering.hasOverflow());

  // clear
  obj1->clear();
  obj2->clear();
}

TEST_F(ObjectUtilTest, map3) { // sparse entries
  auto obj1 = this->map(TYPE::Int, TYPE::Int)
                  .add(Value::createInt(2), Value::createInt(222))
                  .add(Value::createInt(3), Value::createInt(333))
                  .add(Value::createInt(4), Value::createInt(333))
                  .add(Value::createInt(5), Value::createInt(555))
                  .add(Value::createInt(7), Value::createInt(777))
                  .obj;
  auto obj2 = this->map(TYPE::Int, TYPE::Int)
                  .add(Value::createInt(-34), Value::createInt(-222))
                  .add(Value::createInt(2), Value::createInt(222))
                  .add(Value::createInt(4), Value::createInt(444))
                  .add(Value::createInt(5), Value::createInt(555))
                  .obj;
  ASSERT_FALSE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) > 0);
  ASSERT_TRUE(Ordering()(obj2, obj1) < 0);

  // remove entry
  obj1->remove(Value::createInt(7));
  obj1->remove(Value::createInt(3));
  obj1->remove(Value::createInt(4));
  obj2->remove(Value::createInt(4));
  obj2->remove(Value::createInt(-34));
  ASSERT_TRUE(Equality()(obj1, obj2));
  ASSERT_TRUE(Ordering()(obj1, obj2) == 0);
}

TEST_F(ObjectUtilTest, str) {
  ASSERT_NO_FATAL_FAILURE(this->checkStr("", Value()));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("(invalid)", Value::createInvalid()));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("9", Value::createSig(SIGKILL)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("true", Value::createBool(true)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("false", Value::createBool(false)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("-999", Value::createInt(-999)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("3.14", Value::createFloat(3.14)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("0.0", Value::createFloat(0)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("-0.0", Value::createFloat(-0.0)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("Infinity", Value::createFloat(3.14 / 0.0)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("-Infinity", Value::createFloat(3.14 / -0.0)));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("NaN", Value::createFloat(std::nan("0xf"))));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("NaN", Value::createFloat(std::nan(""))));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("", Value::createStr()));
  ASSERT_NO_FATAL_FAILURE(this->checkStr("hello world !!", Value::createStr("hello world !!")));
}

TEST_F(ObjectUtilTest, strCollection1) {
  ASSERT_NO_FATAL_FAILURE(this->checkStr("[]", this->array(TYPE::Int).obj));
  ASSERT_NO_FATAL_FAILURE(this->checkStr(
      "[23, -99]",
      this->array(TYPE::Int).add(Value::createInt(23)).add(Value::createInt(-99)).obj));
  ASSERT_NO_FATAL_FAILURE(this->checkStr(
      "[(invalid), -99]",
      this->array(TYPE::Any).add(Value::createInvalid()).add(Value::createInt(-99)).obj));
  ASSERT_NO_FATAL_FAILURE(this->checkStr(
      "[[], [, hey]]",
      this->array(TYPE::StringArray)
          .add(this->array(TYPE::String).obj)
          .add(this->array(TYPE::String).add(Value::createStr()).add(Value::createStr("hey")).obj)
          .obj));

  ASSERT_NO_FATAL_FAILURE(this->checkStr("[]", this->map(TYPE::Int, TYPE::String).obj));
  ASSERT_NO_FATAL_FAILURE(this->checkStr(
      "[12 : 34]",
      this->map(TYPE::Int, TYPE::Any).add(Value::createInt(12), Value::createInt(34)).obj));
  ASSERT_NO_FATAL_FAILURE(
      this->checkStr("[12 : 34, 99 : (invalid), 0 : hey workd]",
                     this->map(TYPE::Int, TYPE::Any)
                         .add(Value::createInt(12), Value::createInt(34))
                         .add(Value::createInt(99), Value::createInvalid())
                         .add(Value::createInt(0), Value::createStr("hey workd"))
                         .obj));

  ASSERT_NO_FATAL_FAILURE(this->checkStr("(true,)", this->tuple(Value::createBool(true))));
  ASSERT_NO_FATAL_FAILURE(this->checkStr(
      "(true, Infinity)", this->tuple(Value::createBool(true), Value::createFloat(2.0 / 0.0))));
  ASSERT_NO_FATAL_FAILURE(this->checkStr(
      "(true, Infinity,  hey )", this->tuple(Value::createBool(true), Value::createFloat(2.0 / 0.0),
                                             Value::createStr(" hey "))));
}

TEST_F(ObjectUtilTest, strCollection2) {
  {
    auto array = this->array(TYPE::Any).add(Value::createInvalid()).add(Value::createStr("")).obj;
    array->append(array);
    array->append(Value::createBool(false));

    StrAppender appender(StringObject::MAX_SIZE);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsStr(Value::createFloat(3.12)));
    ASSERT_FALSE(stringifier.hasOverflow());
    ASSERT_FALSE(stringifier.addAsStr(array));
    ASSERT_TRUE(stringifier.hasOverflow());
    array->refValues().clear();
  }

  {
    auto map = this->map(TYPE::Int, TYPE::Any)
                   .add(Value::createInt(12), Value::createInvalid())
                   .add(Value::createInt(55), Value::createSig(SIGINT))
                   .add(Value::createInt(5555), Value::createBool(false))
                   .obj;
    map->remove(Value::createInt(55));
    StrAppender appender(StringObject::MAX_SIZE);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsStr(map));
    ASSERT_FALSE(stringifier.hasOverflow());
    ASSERT_EQ("[12 : (invalid), 5555 : false]", appender.get());

    map->insert(Value::createInt(-888), map);
    ASSERT_FALSE(stringifier.addAsStr(map));
    ASSERT_TRUE(stringifier.hasOverflow());
    map->clear();
  }

  {
    auto tuple =
        this->tuple(Value::createBool(false), Value::createStr("hey"), Value::createFloat(-0.0));
    StrAppender appender(StringObject::MAX_SIZE);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsStr(tuple));
    ASSERT_FALSE(stringifier.hasOverflow());
    ASSERT_EQ("(false, hey, -0.0)", appender.get());
    (*tuple)[1] = tuple;
    ASSERT_FALSE(stringifier.addAsStr(tuple));
    ASSERT_TRUE(stringifier.hasOverflow());
    (*tuple)[1] = Value();
  }
}

TEST_F(ObjectUtilTest, interpCollection1) {
  ASSERT_NO_FATAL_FAILURE(this->checkInterp("", this->array(TYPE::Int).obj));
  ASSERT_NO_FATAL_FAILURE(
      this->checkInterp("12", this->array(TYPE::Int).add(Value::createInt(12)).obj));
  ASSERT_NO_FATAL_FAILURE(this->checkInterp(
      "12 34", this->array(TYPE::Int).add(Value::createInt(12)).add(Value::createInt(34)).obj));
  ASSERT_NO_FATAL_FAILURE(this->checkInterp("45 -945", this->array(TYPE::Int)
                                                           .add(Value::createInvalid())
                                                           .add(Value::createInvalid())
                                                           .add(Value::createInt(45))
                                                           .add(Value::createInvalid())
                                                           .add(Value::createInt(-945))
                                                           .add(Value::createInvalid())
                                                           .obj));

  ASSERT_NO_FATAL_FAILURE(this->checkInterp("", this->map(TYPE::Int, TYPE::String).obj));
  ASSERT_NO_FATAL_FAILURE(
      this->checkInterp("89  90 @@@ 99 AAA", this->map(TYPE::Int, TYPE::String)
                                                 .add(Value::createInt(23), Value::createInvalid())
                                                 .add(Value::createInt(56), Value::createInvalid())
                                                 .add(Value::createInt(89), Value::createStr())
                                                 .add(Value::createInt(90), Value::createStr("@@@"))
                                                 .add(Value::createInt(99), Value::createStr("AAA"))
                                                 .add(Value::createInt(599), Value::createInvalid())
                                                 .obj));

  ASSERT_NO_FATAL_FAILURE(this->checkInterp("true", this->tuple(Value::createBool(true))));
  ASSERT_NO_FATAL_FAILURE(this->checkInterp(
      "true Infinity  hey ", this->tuple(Value::createBool(true), Value::createFloat(2.0 / 0.0),
                                         Value::createStr(" hey "))));
  auto obj = this->tuple(Value::createBool(true), Value::createFloat(-2.0 / 0.0),
                         Value::createStr("hey"), Value::createBool(false), Value::createInt(34));
  typeAs<BaseObject>(obj)[0] = Value::createInvalid();
  typeAs<BaseObject>(obj)[1] = Value::createInvalid();
  typeAs<BaseObject>(obj)[4] = Value::createInvalid();
  ASSERT_NO_FATAL_FAILURE(this->checkInterp("hey false", obj));
}

TEST_F(ObjectUtilTest, interpCollection2) {
  {
    auto array = this->array(TYPE::Any).add(Value::createInvalid()).add(Value::createStr("")).obj;
    array->append(array);
    array->append(Value::createBool(false));

    StrAppender appender(StringObject::MAX_SIZE);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsInterp(Value::createFloat(3.12)));
    ASSERT_FALSE(stringifier.hasOverflow());
    ASSERT_FALSE(stringifier.addAsInterp(array));
    ASSERT_TRUE(stringifier.hasOverflow());
    array->refValues().clear();
  }

  {
    auto map = this->map(TYPE::Int, TYPE::Any)
                   .add(Value::createInt(12), Value::createInvalid())
                   .add(Value::createInt(55), Value::createSig(SIGINT))
                   .add(Value::createInt(5555), Value::createBool(false))
                   .obj;
    map->remove(Value::createInt(55));
    StrAppender appender(StringObject::MAX_SIZE);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsInterp(map));
    ASSERT_FALSE(stringifier.hasOverflow());
    ASSERT_EQ("5555 false", appender.get());

    map->insert(Value::createInt(-888), map);
    ASSERT_FALSE(stringifier.addAsInterp(map));
    ASSERT_TRUE(stringifier.hasOverflow());
    map->clear();
  }

  {
    auto tuple =
        this->tuple(Value::createBool(false), Value::createStr("hey"), Value::createFloat(-0.0));
    StrAppender appender(StringObject::MAX_SIZE);
    Stringifier stringifier(this->pool, appender);
    ASSERT_TRUE(stringifier.addAsInterp(tuple));
    ASSERT_FALSE(stringifier.hasOverflow());
    ASSERT_EQ("false hey -0.0", appender.get());
    (*tuple)[1] = tuple;
    ASSERT_FALSE(stringifier.addAsInterp(tuple));
    ASSERT_TRUE(stringifier.hasOverflow());
    (*tuple)[1] = Value();
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}