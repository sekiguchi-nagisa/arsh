
#ifndef YDSH_TEST_ARG_PARSER_HELPER_HPP
#define YDSH_TEST_ARG_PARSER_HELPER_HPP

#include "arg_parser_base.h"
#include "type_pool.h"

namespace ydsh {

class ArgEntriesBuilder {
private:
  std::vector<ArgEntry> values;
  unsigned int fieldOffset{0};

public:
  template <typename Func>
  static constexpr bool func_requirement_v =
      std::is_same_v<void, std::invoke_result_t<Func, ArgEntry &>>;

  template <typename Func, enable_when<func_requirement_v<Func>> = nullptr>
  ArgEntriesBuilder &add(unsigned char v, Func func) {
    auto index = this->getCurIndex();
    this->values.emplace_back(index, v);
    func(this->values.back());
    return *this;
  }

  template <typename Func, enable_when<func_requirement_v<Func>> = nullptr>
  ArgEntriesBuilder &add(Func func) {
    return this->add(this->fieldOffset++, std::move(func));
  }

  ArgEntriesBuilder &addHelp() {
    this->values.push_back(ArgEntry::newHelp(this->getCurIndex()));
    return *this;
  }

  std::vector<ArgEntry> build() && { return std::move(this->values); }

private:
  ArgEntryIndex getCurIndex() const { return static_cast<ArgEntryIndex>(this->values.size()); }
};

inline const CLIRecordType &createRecordType(TypePool &pool, const char *typeName,
                                             ArgEntriesBuilder &&builder, ModId modId) {
  auto ret = pool.createCLIRecordType(typeName, modId);
  assert(ret);
  (void)ret;
  auto entries = std::move(builder).build();
  std::unordered_map<std::string, HandlePtr> handles;
  for (size_t i = 0; i < entries.size(); i++) {
    std::string name = "field_";
    name += std::to_string(i);
    auto handle = HandlePtr::create(pool.get(TYPE::String), i, HandleKind::VAR,
                                    HandleAttr::UNCAPTURED, modId);
    handles.emplace(std::move(name), std::move(handle));
  }
  auto &type = *cast<CLIRecordType>(ret.asOk());
  ret = pool.finalizeCLIRecordType(type, std::move(handles), std::move(entries));
  assert(ret);
  return type;
}

} // namespace ydsh

#endif // YDSH_TEST_ARG_PARSER_HELPER_HPP
