
#ifndef ARSH_PAGER_TEST_BASE_HPP
#define ARSH_PAGER_TEST_BASE_HPP

#include "../test_common.h"

#include <candidates.h>
#include <line_renderer.h>
#include <object.h>
#include <pager.h>
#include <vm.h>

class PagerTest : public ExpectOutput {
protected:
  ARState *state;
  CharWidthProperties ps;

public:
  PagerTest() {
    this->state = ARState_create();
    this->ps.replaceInvalid = true;
  }

  ~PagerTest() override { ARState_delete(&this->state); }

  void append(CandidatesObject &) {}

  template <typename... T>
  void append(CandidatesObject &wrapper, const char *first, T &&...remain) {
    wrapper.addNewCandidateFrom(*this->state, std::string(first), false);
    this->append(wrapper, std::forward<T>(remain)...);
  }

  template <typename... T>
  ObjPtr<CandidatesObject> create(T &&...args) {
    auto obj = createObject<CandidatesObject>();
    this->append(*obj, std::forward<T>(args)...);
    return obj;
  }

  ObjPtr<CandidatesObject> createWith(std::vector<std::pair<const char *, const char *>> &&args,
                                      const CandidateAttr attr = {CandidateAttr::Kind::NONE,
                                                                  false}) {
    auto obj = createObject<CandidatesObject>();
    for (auto &[can, sig] : args) {
      obj->addNewCandidateWith(*this->state, can, sig, attr);
    }
    return obj;
  }

  std::string render(const ArrayPager &pager) const {
    std::string out;
    LineRenderer renderer(this->ps, 0, out);
    pager.render(renderer);
    return out;
  }
};

#endif // ARSH_PAGER_TEST_BASE_HPP
