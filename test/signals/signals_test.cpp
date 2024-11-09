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

static std::string normalize(const std::string &str) {
  std::string ret = str;
  std::transform(str.begin(), str.end(), ret.begin(), ::toupper);
  if (arsh::StringRef(ret).startsWith("SIG")) {
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
  int invalidCount = 0;
  std::vector<SigEntry> entries;
  for (auto &e : split(str)) {
    auto v = normalize(e);
    if (v.empty() || !std::isalpha(v[0]) || arsh::StringRef(v).startsWith("RT")) {
      continue;
    }
    auto *entry = arsh::findSignalEntryByName(v);
    int num;
    if (entry) {
      num = entry->sigNum;
      v = arsh::findSignalEntryByNum(entry->sigNum)->abbrName; // resolve actual name
    } else {
      invalidCount++;
      num = -invalidCount;
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
  auto list = arsh::toSortedUniqueSignalEntries();
  for (auto &e : list) {
    if (e.isRealTime()) {
      continue;
    }
    values.push_back(format(e.abbrName, e.sigNum));
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

  ASSERT_EQ(SIGQUIT, findSignalEntryByName("quit")->sigNum);
  ASSERT_EQ(SIGQUIT, findSignalEntryByName("Sigquit")->sigNum);
  ASSERT_EQ(SIGTSTP, findSignalEntryByName("tStP")->sigNum);
  ASSERT_EQ(SIGTSTP, findSignalEntryByName("SigTStp")->sigNum);
  ASSERT_EQ(nullptr, findSignalEntryByName("HOGED"));

  char b[] = "INT\0";
  ASSERT_EQ(nullptr, findSignalEntryByName(StringRef(b, std::size(b) - 1)));

  ASSERT_STREQ("USR1", findSignalEntryByNum(SIGUSR1)->abbrName);
  ASSERT_STREQ("SEGV", findSignalEntryByNum(SIGSEGV)->abbrName);
  ASSERT_EQ(nullptr, findSignalEntryByNum(-12));
  ASSERT_EQ("SIGUSR2", findSignalEntryByNum(SIGUSR2)->toFullName());
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
