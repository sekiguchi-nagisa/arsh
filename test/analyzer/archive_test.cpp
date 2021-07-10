
#include "gtest/gtest.h"

#include "archive.h"
#include "ast.h"

using namespace ydsh::lsp;
using namespace ydsh;

static ASTContextPtr newctx(unsigned int modId) {
  std::string path = "file:///dummy_";
  path += std::to_string(modId);
  return ASTContextPtr::create(modId, uri::URI::fromString(path), "#hello", 1);
}

class ArchiveTest : public ::testing::Test {
private:
  ASTContextPtr orgCtx;
  ASTContextPtr newCtx;

protected:
  const unsigned int builtinIdOffset;

public:
  ArchiveTest()
      : orgCtx(newctx(1)), newCtx(newctx(2)),
        builtinIdOffset(this->orgCtx->getPool().getDiscardPoint().typeIdOffset) {}

  TypePool &pool() { return this->orgCtx->getPool(); }

  void defineAndArchive(const char *fieldName, const char *typeName, FieldAttribute attr = {}) {
    ASSERT_TRUE(typeName);
    auto ret = this->orgCtx->getPool().getType(typeName);
    ASSERT_TRUE(ret);
    this->defineAndArchive(fieldName, *ret.asOk(), attr);
  }

  void defineAndArchive(const char *fieldName, const DSType &orgType, FieldAttribute attr = {}) {
    ASSERT_TRUE(fieldName);
    ASSERT_TRUE(orgType.typeId() < this->pool().getDiscardPoint().typeIdOffset);
    ASSERT_EQ(orgType, this->orgCtx->getPool().get(orgType.typeId()));

    // define handle
    auto ret1 = this->orgCtx->getScope()->defineHandle(fieldName, orgType, attr);
    ASSERT_TRUE(ret1);
    auto &orgHandle = *ret1.asOk();

    // serialize
    auto archive =
        Archive::pack(this->orgCtx->getPool(), this->builtinIdOffset, fieldName, orgHandle);
    ASSERT_EQ(fieldName, archive.getName());
    ASSERT_FALSE(archive.getData().empty());

    // deserialize
    auto ret2 = archive.unpack(this->newCtx->getPool()); // deserialize in another context
    ASSERT_TRUE(ret2.hasValue());
    auto &newHandle = ret2.unwrap();

    // compare 2 handles
    ASSERT_EQ(orgHandle.getModID(), newHandle.getModID());
    ASSERT_EQ(orgHandle.getIndex(), newHandle.getIndex());
    ASSERT_EQ(orgHandle.getCommitID(), newHandle.getCommitID());
    ASSERT_TRUE(newHandle.getTypeID() <= this->newCtx->getPool().getDiscardPoint().typeIdOffset);
    auto &newType = this->newCtx->getPool().get(newHandle.getTypeID());
    ASSERT_EQ(orgType.getNameRef(), newType.getNameRef());
  }
};

TEST_F(ArchiveTest, predefined) {
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("a", "[String]"));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("b", "Void", FieldAttribute::ENV));
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("c", "((String, [String]) -> Void)!", FieldAttribute::FUNC_HANDLE));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("d", "(Signal) -> Void", FieldAttribute::RANDOM));
}

TEST_F(ArchiveTest, array) {
  //
  auto ret1 = this->pool().createArrayType(this->pool().get(TYPE::GlobbingError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa", type1));
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("bbb", type1, FieldAttribute::SECONDS | FieldAttribute::ALIAS));

  //
  ret1 = this->pool().createArrayType(type1);
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_TRUE(type2.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", "[[GlobbingError]]"));
}

TEST_F(ArchiveTest, map) {
  //
  auto ret1 = this->pool().createMapType(this->pool().get(TYPE::Signal),
                                         this->pool().get(TYPE::GlobbingError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa", type1, FieldAttribute::READ_ONLY));
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("bbb", type1, FieldAttribute::RANDOM | FieldAttribute::ALIAS));

  //
  ret1 =
      this->pool().createMapType(this->pool().get(TYPE::String), this->pool().get(TYPE::Boolean));
  ASSERT_TRUE(ret1);
  ret1 = this->pool().createMapType(this->pool().get(TYPE::Float), *ret1.asOk());
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_TRUE(type2.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", type2));
}

TEST_F(ArchiveTest, tuple) {
  //
  std::vector<const DSType *> types;
  types.push_back(&this->pool().get(TYPE::IllegalAccessError));
  types.push_back(&this->pool().get(TYPE::TypeCastError));
  types.push_back(&this->pool().get(TYPE::StringArray));
  auto ret1 = this->pool().createTupleType(std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("aaa12", type1, FieldAttribute::READ_ONLY));

  //
  ret1 = this->pool().createMapType(this->pool().get(TYPE::Int), this->pool().get(TYPE::Int));
  ASSERT_TRUE(ret1);
  types = std::vector<const DSType *>();
  types.push_back(ret1.asOk());
  ret1 = this->pool().createTupleType(std::move(types));
  ASSERT_TRUE(ret1);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("bbb", "([Int : Int],)"));
}

TEST_F(ArchiveTest, option) {
  //
  auto ret1 = this->pool().createOptionType(this->pool().get(TYPE::UnwrappingError));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("w1", "UnwrappingError!", FieldAttribute::ALIAS));

  //
  ret1 = this->pool().createOptionType(type1);
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_EQ(type1, type2);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("w2", type2));

  //
  ret1 = this->pool().createArrayType(type2);
  ASSERT_TRUE(ret1);
  ret1 = this->pool().createOptionType(*ret1.asOk());
  ASSERT_TRUE(ret1);
  auto &type3 = *ret1.asOk();
  ASSERT_TRUE(type3.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", type3));
}

TEST_F(ArchiveTest, func) {
  //
  std::vector<const DSType *> types;
  auto ret1 = this->pool().createFuncType(this->pool().get(TYPE::Void), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type1 = *ret1.asOk();
  ASSERT_TRUE(type1.typeId() >= this->builtinIdOffset);
  ASSERT_NO_FATAL_FAILURE(
      this->defineAndArchive("e1", type1, FieldAttribute::FUNC_HANDLE | FieldAttribute::READ_ONLY));

  //
  types = std::vector<const DSType *>();
  types.push_back(this->pool().getType("(Signal) -> Void").asOk());
  types.push_back(&type1);
  ret1 = this->pool().createTupleType(std::vector<const DSType*>(types));
  ASSERT_TRUE(ret1);
  types.push_back(ret1.asOk());
  ret1 = this->pool().createFuncType(this->pool().get(TYPE::Nothing), std::move(types));
  ASSERT_TRUE(ret1);
  auto &type2 = *ret1.asOk();
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("ccc", type2, FieldAttribute::ALIAS));
  ASSERT_NO_FATAL_FAILURE(this->defineAndArchive("DDD", type2, FieldAttribute::FUNC_HANDLE));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}