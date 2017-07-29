//
// Created by skgchxngsxyz-carbon on 16/12/25.
//

#ifndef YDSH_TEST_COMMON_HPP
#define YDSH_TEST_COMMON_HPP

#include <cstdlib>
#include <vector>
#include <string>

#include <misc/size.hpp>

// common utility for test

class TempFileFactory {
protected:
    std::string tmpFileName;

    TempFileFactory() = default;
    virtual ~TempFileFactory() = default;

    void createTemp() {
        const char *tmpdir = getenv("TMPDIR");
        if(tmpdir == nullptr) {
            tmpdir = "/tmp";
        }
        unsigned int size = 512;
        char name[size];
        snprintf(name, size, "%s/exec_test_tmpXXXXXX", tmpdir);

        int fd = mkstemp(name);
        close(fd);
        this->tmpFileName = name;
    }

    void deleteTemp() {
        remove(this->tmpFileName.c_str());
    }

public:
    const std::string &getTmpFileName() const {
        return this->tmpFileName;
    }
};


class CommandBuilder {
private:
    std::vector<std::string> args;

public:
    CommandBuilder(const char *cmdName) : args{cmdName} {}
    ~CommandBuilder() = default;

    CommandBuilder &addArg(const char *arg) {
        this->args.push_back(arg);
        return *this;
    }

    CommandBuilder &addArg(const std::string &str) {
        return this->addArg(str.c_str());
    }

    CommandBuilder &addArgs(const std::vector<std::string> &values);

    /**
     * get concatenated command string.
     * @return
     */
    std::string format() const;

    int exec() const;

    std::string execAndGetOutput(bool removeLastSpace = true) const;
};

inline bool isSpace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

CommandBuilder& CommandBuilder::addArgs(const std::vector<std::string> &values) {
    for(auto &e : values) {
        this->args.push_back(e);
    }
    return *this;
}

std::string CommandBuilder::format() const {
    std::string str = this->args[0];
    for(unsigned int i = 1; i < this->args.size(); i++) {
        str += ' ';
        str += '"';
        str += this->args[i];
        str += '"';
    }
    return str;
}

int CommandBuilder::exec() const {
    auto cmd = this->format();
    int ret = system(cmd.c_str());
    return ret;
}

std::string CommandBuilder::execAndGetOutput(bool removeLastSpace) const {
    auto cmd = this->format();
    FILE *fp = popen(cmd.c_str(), "r");
    std::string out;
    char buf[256];
    for(int readSize; (readSize = fread(buf, sizeof(char), ydsh::arraySize(buf), fp)) > 0;) {
        out.append(buf, readSize);
    }
    pclose(fp);

    // remove last newline
    if(removeLastSpace) {
        while(!out.empty() && isSpace(out.back())) {
            out.pop_back();
        }
    }
    return out;
}

#define ASSERT_(F) do { SCOPED_TRACE(""); F; } while(false)

#endif //YDSH_TEST_COMMON_HPP
