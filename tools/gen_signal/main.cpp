/*
 * Copyright (C) 2017 Nagisa Sekiguchi
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

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

#include <misc/size.hpp>

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
    while(!out.empty() && out.back() == '\n') {
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
    std::string out;
    for(auto &ch : str) {
        if(ch >= 'a' && ch <= 'z') {
            ch -= static_cast<int>('a') - static_cast<int>('A');   // convert to upper character
        }
        out += ch;
    }
    str = std::move(out);
}

static bool startsWith(const char *s1, const char *s2) {
    return s1 != nullptr && s2 != nullptr && strstr(s1, s2) == s1;
}

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "[usage] %s [output path]\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];

    std::string killPath = Command("which").addArg("kill")();
    std::string killOut = Command(killPath.c_str()).addArg("-l")();

    auto values = split(killOut);
    for(auto &e : values) {
        toUpperCase(e);
    }

    // generate header
    FILE *fp = fopen(path, "w");
    fprintf(fp, "// this is a auto-generated file. not change it directly\n"
                "#ifndef GEN_SIGNAL__SUPPORTED_SIGNAL_H\n"
                "#define GEN_SIGNAL__SUPPORTED_SIGNAL_H\n\n");

    for(auto &v : values) {
        if(startsWith(v.c_str(), "RT")) {
            continue;
        }
        fprintf(fp, "{\"%s\", SIG%s},\n", v.c_str(), v.c_str());
    }
    fprintf(fp, "\n#endif // GEN_SIGNAL__SUPPORTED_SIGNAL_H\n");
    fclose(fp);

    return 0;
}