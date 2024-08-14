#include "gtest/gtest.h"

#include <algorithm>
#include <csignal>
#include <cstring>

#include <signals.h>

#include "../test_common.h"

static std::vector<std::string> split(const std::string &str) {
  std::vector<std::string> bufs;
  std::string buf;
  for (auto &ch : str) {
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
      bufs.push_back(std::move(buf));
      buf = "";
    } else {
      buf += ch;
    }
  }
  if (!buf.empty()) {
    bufs.push_back(std::move(buf));
  }
  return bufs;
}

static bool startsWith(const char *s1, const char *s2) {
  return s1 != nullptr && s2 != nullptr && strstr(s1, s2) == s1;
}

static bool isRT(const std::string &str) { return startsWith(str.c_str(), "RT"); }

static std::string normalize(const std::string &str) {
  std::string ret = str;
  std::transform(str.begin(), str.end(), ret.begin(), ::toupper);
  if (startsWith(ret.c_str(), "SIG")) {
    ret = ret.substr(3);
  }
  return ret;
}

static std::string format(const char *name, int num) {
  char value[64];
  snprintf(value, std::size(value), "%s(%d)", name, num);
  return std::string(value);
}

struct SigEntry {
  std::string name;
  int num;

  friend bool operator<(const SigEntry &x, const SigEntry &y) { return x.num < y.num; }

  friend bool operator==(const SigEntry &x, const SigEntry &y) { return x.num == y.num; }
};

static std::string formatEntry(const SigEntry &e) { return format(e.name.c_str(), e.num); }

static std::vector<std::string> toSignalList(const std::string &str) {
  unsigned int invalidCount = 0;
  std::vector<SigEntry> entries;
  for (auto &e : split(str)) {
    auto v = normalize(e);
    if (v.empty() || !std::isalpha(v[0]) || isRT(v)) {
      continue;
    }
    int num = arsh::getSignalNum(v.c_str());
    if (num < 0) {
      invalidCount++;
      num = -invalidCount;
    }
    if (num > -1) {
      v = arsh::getSignalName(num);
    }
    entries.push_back({.name = std::move(v), .num = num});
  }

  // dedup
  std::sort(entries.begin(), entries.end());
  entries.erase(std::unique(entries.begin(), entries.end()), entries.end());

  std::vector<std::string> ret;
  std::transform(entries.begin(), entries.end(), std::back_inserter(ret), formatEntry);
  return ret;
}

static std::vector<std::string> getSignalList() {
  std::vector<std::string> values;
  for (auto &e : arsh::getUniqueSignalList()) {
    values.push_back(format(arsh::getSignalName(e), e));
  }
  return values;
}

static std::string join(const std::vector<std::string> &values, char delim) {
  std::string ret;
  for (auto &e : values) {
    if (!ret.empty()) {
      ret += delim;
    }
    ret += e;
  }
  return ret;
}

TEST(Signal, all) {
  std::string killOut = ProcBuilder{"/bin/kill", "-l"}.execAndGetResult().out;
  ASSERT_TRUE(!killOut.empty());

  auto expected = toSignalList(killOut);
  auto actual = getSignalList();

  ASSERT_EQ(join(expected, '\n'), join(actual, '\n'));
}

TEST(Signal, base) {
  using namespace arsh;

  ASSERT_EQ(SIGQUIT, getSignalNum("quit"));
  ASSERT_EQ(SIGQUIT, getSignalNum("Sigquit"));
  ASSERT_EQ(SIGTSTP, getSignalNum("tStP"));
  ASSERT_EQ(SIGTSTP, getSignalNum("SigTStp"));
  ASSERT_EQ(-1, getSignalNum("HOGED"));

  char b[] = "INT\0";
  ASSERT_EQ(-1, getSignalNum(StringRef(b, std::size(b) - 1)));

  ASSERT_STREQ("USR1", getSignalName(SIGUSR1));
  ASSERT_STREQ("SEGV", getSignalName(SIGSEGV));
  ASSERT_EQ(nullptr, getSignalName(-12));
}

TEST(Signal, sigset) {
  using namespace arsh;

  AtomicSigSet set;

  ASSERT_TRUE(set.empty());
  set.add(SIGHUP);
  set.add(SIGCHLD);
  ASSERT_TRUE(set.has(SIGHUP));
  ASSERT_TRUE(set.has(SIGCHLD));
  ASSERT_FALSE(set.has(SIGTERM));

  set.del(SIGTERM);
  ASSERT_FALSE(set.has(SIGTERM));
  set.del(SIGHUP);
  ASSERT_FALSE(set.has(SIGHUP));

  ASSERT_FALSE(set.empty());
  set.del(SIGCHLD);
  ASSERT_TRUE(set.empty());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
