/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

/**
 * Integration test for the standalone `rematch` tool (test/regex/rematch.cpp).
 *
 * It exercises every engine that was enabled at configure time (REMATCH_ENGINES) with the
 * `-m` behavior of `redump`: the same output contract (`input: ...` / `(offset=, size=)` /
 * `failed`) but produced by the embedded Hermes / QuickJS / SRELL engines. The cases are
 * engine-independent so the test verifies the adapters and the CLI, not the engines'
 * language coverage.
 */

#include "../test_common.h"

#ifndef REMATCH_PATH
#error require REMATCH_PATH
#endif

using namespace arsh;
using process::ProcBuilder;
using process::WaitStatus;

namespace {

std::vector<std::string> getEngines() {
  std::vector<std::string> engines;
  std::string current;
  for (char ch : std::string(REMATCH_ENGINES)) {
    if (ch == ' ') {
      if (!current.empty()) {
        engines.push_back(std::move(current));
        current.clear();
      }
    } else {
      current += ch;
    }
  }
  if (!current.empty()) {
    engines.push_back(std::move(current));
  }
  return engines;
}

struct RematchTest : public ::testing::TestWithParam<std::string> {
  void expect(const std::vector<std::string> &args, int status, const std::string &out) {
    auto builder = ProcBuilder(REMATCH_PATH);
    builder.addArg("-e").addArg(this->GetParam());
    for (auto &arg : args) {
      builder.addArg(arg);
    }
    auto result = builder.execAndGetResult(false);
    EXPECT_EQ(status, result.status.value);
    EXPECT_EQ(out, result.out);
    ASSERT_FALSE(this->HasFailure());
  }
};

TEST_P(RematchTest, basic) {
  // (a) with the whole match and the capture group
  this->expect({"-m", "12a", "(a)", ""}, 0, "input: `12a'\n(offset=2, size=1)\n(offset=2, size=1)\n");

  // no match
  this->expect({"-m", "", ".", ""}, 1, "input: `'\nfailed\n");

  // empty pattern matches an empty input
  this->expect({"-m", "", "", ""}, 0, "input: `'\n(offset=0, size=0)\n");

  // a non-BMP character is reported as its UTF-8 byte size in BMP mode
  this->expect({"-m", "\U00024155", ".", ""}, 0, "input: `\U00024155'\n(offset=0, size=4)\n");

  // a multi-byte character capture group
  this->expect({"-m", "12aあr", "(あ)", ""}, 0,
               "input: `12aあr'\n(offset=3, size=3)\n(offset=3, size=3)\n");

  // the second alternative matched, the first group is unset
  this->expect({"-m", "cd", "(ab)|(cd)", ""}, 0,
               "input: `cd'\n(offset=0, size=2)\n(unset)\n(offset=0, size=2)\n");
}

TEST_P(RematchTest, modifiers) {
  // ignore case
  this->expect({"-m", "ABC", "abc", "i"}, 0, "input: `ABC'\n(offset=0, size=3)\n");

  // multiline: '^' matches after a newline
  this->expect({"-m", "x\ny", "^y", "m"}, 0, "input: `x\ny'\n(offset=2, size=1)\n");

  // dotall: '.' matches a newline
  this->expect({"-m", "\n", ".", "s"}, 0, "input: `\n'\n(offset=0, size=1)\n");

  // invalid modifier
  this->expect({"-m", "x", "x", "z"}, 1, "");
}

TEST_P(RematchTest, noInput) {
  // without -m the input is empty, matching redump's non -m behavior
  this->expect({"a"}, 1, "input: `'\nfailed\n");
}

INSTANTIATE_TEST_SUITE_P(RematchTest, RematchTest, ::testing::ValuesIn(getEngines()));

TEST(RematchTest, help) {
  auto result = ProcBuilder(REMATCH_PATH).addArg("-h").execAndGetResult();
  EXPECT_EQ(2, result.status.value);
  EXPECT_THAT(result.out, ::testing::HasSubstr("engines:"));
}

TEST(RematchTest, unknownEngine) {
  auto result =
      ProcBuilder(REMATCH_PATH).addArg("-e").addArg("no-such-engine").addArg("a").execAndGetResult();
  EXPECT_EQ(1, result.status.value);
  EXPECT_THAT(result.err, ::testing::HasSubstr("unknown engine"));
}

} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
