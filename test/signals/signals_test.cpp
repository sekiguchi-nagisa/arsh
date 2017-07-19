#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <csignal>

#include <misc/size.hpp>
#include <signals.h>

class Command {
private:
    std::vector<std::string> args;

public:
    Command(const char *cmdName) : args{cmdName} {}
    ~Command() = default;

    Command &addArg(const char *arg) {
        this->args.push_back(arg);
        return *this;
    }

    std::string operator()() const;
};

static bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string Command::operator()() const {
    std::string cmd = this->args[0];
    for(unsigned int i = 1; i < this->args.size(); i++) {
        cmd += " ";
        cmd += this->args[i];
    }

    FILE *fp = popen(cmd.c_str(), "r");
    std::string out;
    char buf[256];
    for(int readSize; (readSize = fread(buf, sizeof(char), ydsh::arraySize(buf), fp)) > 0;) {
        out.append(buf, readSize);
    }
    pclose(fp);

    // remove last newline
    while(!out.empty() && isSpace(out.back())) {
        out.pop_back();
    }
    return out;
}

static std::vector<std::string> split(const std::string &str) {
    std::vector<std::string> bufs;
    std::string buf;
    for(auto &ch : str) {
        if(ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
            bufs.push_back(std::move(buf));
            buf = "";
        } else {
            buf += ch;
        }
    }
    if(!buf.empty()) {
        bufs.push_back(std::move(buf));
    }
    return bufs;
}

static void toUpperCase(std::string &str) {
    for(auto &ch : str) {
        if(ch >= 'a' && ch <= 'z') {
            ch -= static_cast<int>('a') - static_cast<int>('A');   // convert to upper character
        }
    }
}

static bool startsWith(const char *s1, const char *s2) {
    return s1 != nullptr && s2 != nullptr && strstr(s1, s2) == s1;
}

static bool excludeRT(const std::string &str) {
    return startsWith(str.c_str(), "RT");
}

static void trim(std::string &str) {
    if(startsWith(str.c_str(), "SIG")) {
        str = str.c_str() + 3;
    }
}

/**
 * exclude other signal (non-standard signal) and real time signal
 *
 */
struct SigFilter {
    bool operator()(const std::string &str) const {
        const char *v[] = {
                "IOT", "EMT", "STKFLT", "IO","CLD", "PWR", "INFO", "LOST", "WINCH", "UNUSED"
        };
        for(auto &e : v) {
            if(str == e) {
                return true;
            }
        }
        return false;
    }
};

static std::vector<std::string> toSignalList(const std::string &str) {
    auto values = split(str);
    for(auto iter = values.begin(); iter != values.end();) {
        toUpperCase(*iter);
        trim(*iter);
        auto &e = *iter;
        if(e.empty() || !std::isalpha(e[0]) || excludeRT(e)) {
            iter = values.erase(iter);
            continue;
        }
        ++iter;
    }
    std::sort(values.begin(), values.end());
    values.erase(std::remove_if(values.begin(), values.end(), SigFilter()), values.end());
    return values;
}

static std::vector<std::string> toList(const ydsh::SignalPair *pairs) {
    std::vector<std::string> values;
    for(; pairs->name; pairs++) {
        values.push_back(std::string(pairs->name));
    }
    std::sort(values.begin(), values.end());
    values.erase(std::remove_if(values.begin(), values.end(), SigFilter()), values.end());
    return values;
}


TEST(Signal, all) {
    std::string killPath = Command("which").addArg("kill")();
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(killPath.size() > 0));

    std::string killOut = Command(killPath.c_str()).addArg("-l")();
    ASSERT_NO_FATAL_FAILURE(ASSERT_TRUE(killOut.size() > 0));

    auto expected = toSignalList(killOut);
    auto actual = toList(ydsh::getSignalList());

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expected.size(), actual.size()));
    unsigned int size = expected.size();
    for(unsigned int i = 0; i < size; i++) {
        ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(expected[i], actual[i]));
    }
}

TEST(Signal, base) {
    using namespace ydsh;

    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(SIGQUIT, getSignalNum("quit")));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(SIGQUIT, getSignalNum("Sigquit")));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(SIGTSTP, getSignalNum("tStP")));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(SIGTSTP, getSignalNum("SigTStp")));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(-1, getSignalNum("HOGED")));

    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("USR1", getSignalName(SIGUSR1)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_STREQ("SEGV", getSignalName(SIGSEGV)));
    ASSERT_NO_FATAL_FAILURE(ASSERT_EQ(nullptr, getSignalName(-12)));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
