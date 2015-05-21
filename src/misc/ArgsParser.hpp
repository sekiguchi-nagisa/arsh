/*
 * Copyright (C) 2015 Nagisa Sekiguchi
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

#ifndef MISC_ARGSPARSER_H_
#define MISC_ARGSPARSER_H_

#include <string>
#include <vector>
#include <ostream>

namespace ydsh {
namespace args {

struct ParseError {
    ParseError(const char *message, const char *suffix) :
            message(message), suffix(suffix) {
    }

    /**
     * not delete it.
     */
    const char *message;

    /**
     * message suffix.
     * not delete it.
     */
    const char *suffix;
};

template<typename OPTION_ENUM>
class ArgsParser {
private:
    template<typename OPTION_ENUM2>
    struct Option {
        OPTION_ENUM2 optionId;
        const char *optionSymbol;
        bool hasArg;
        const char *description;

        unsigned int getUsageSize() const;

        std::vector<std::string> getDescriptions() const;

        static const char *usageSuffix;
    };

    std::vector<Option<OPTION_ENUM>> options;

public:
    ArgsParser();

    ~ArgsParser();

    ArgsParser *addOption(OPTION_ENUM optionId, const char *optionSymbol,
                          bool hasArg = false, const char *description = "");

    /**
     * write parsed options to cmdLines.
     * if has rest argument, write to restArgs.
     * if parse success, return rest arguments
     */
    std::vector<const char *> parse(int argc, char **argv,
                                    std::vector<std::pair<OPTION_ENUM, const char *>> &cmdLines) throw(ParseError);

    void printHelp(std::ostream &stream);
};

// ####################
// ##     Option     ##
// ####################

template<typename OPTION_ENUM>
template<typename OPTION_ENUM2>
unsigned int ArgsParser<OPTION_ENUM>::Option<OPTION_ENUM2>::getUsageSize() const {
    return strlen(this->optionSymbol) + (this->hasArg ? strlen(usageSuffix) : 0);
}

template<typename OPTION_ENUM>
template<typename OPTION_ENUM2>
std::vector<std::string> ArgsParser<OPTION_ENUM>::Option<OPTION_ENUM2>::getDescriptions() const {
    std::vector<std::string> bufs;
    std::string buf;
    for(unsigned int i = 0; this->description[i] != '\0'; i++) {
        char ch = this->description[i];
        if(ch == '\n') {
            if(!buf.empty()) {
                bufs.push_back(std::move(buf));
                buf.clear();
            }
        } else {
            buf += ch;
        }
    }
    if(!buf.empty()) {
        bufs.push_back(std::move(buf));
    }
    return bufs;
}

template<typename OPTION_ENUM>
template<typename OPTION_ENUM2>
const char *ArgsParser<OPTION_ENUM>::Option<OPTION_ENUM2>::usageSuffix = " <arg>";

// ########################
// ##     ArgsParser     ##
// ########################

template<typename OPTION_ENUM>
ArgsParser<OPTION_ENUM>::ArgsParser() :
        options() {
}

template<typename OPTION_ENUM>
ArgsParser<OPTION_ENUM>::~ArgsParser() {
}

template<typename OPTION_ENUM>
ArgsParser<OPTION_ENUM> *ArgsParser<OPTION_ENUM>::addOption(OPTION_ENUM optionId, const char *optionSymbol,
                                  bool hasArg, const char *description) {
    this->options.push_back({optionId, optionSymbol, hasArg, description});
    return this;
}

template<typename OPTION_ENUM>
std::vector<const char *> ArgsParser<OPTION_ENUM>::parse(int argc, char **argv,
                                            std::vector<std::pair<OPTION_ENUM, const char *>> &cmdLines) throw(ParseError) {
    static char empty[] = "";
    std::vector<const char *> restArgs;

    for(int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        bool findOption = false;
        for(auto &option : this->options) {
            if(strcmp(arg, option.optionSymbol) == 0) { // match option
                if(option.hasArg) {
                    if(i + 1 < argc) {
                        cmdLines.push_back(std::make_pair(option.optionId, argv[++i]));
                    } else {
                        throw ParseError("expect for argument", arg);
                    }
                } else {
                    cmdLines.push_back(std::make_pair(option.optionId, empty));
                }
                findOption = true;
                break;
            } else if(strlen(arg) == 0 || arg[0] != '-') { // get rest argument
                for(int j = i; j < argc; j++) {
                    restArgs.push_back(argv[j]);
                }
                return restArgs;
            }
        }
        if(!findOption) {
            throw ParseError("illegal argument", arg);
        }
    }
    return restArgs;
}

template<typename OPTION_ENUM>
void ArgsParser<OPTION_ENUM>::printHelp(std::ostream &stream) {
    unsigned int maxSizeOfUsage = 0;

    // compute usage size
    for(auto &option : this->options) {
        unsigned int size = option.getUsageSize();
        if(size > maxSizeOfUsage) {
            maxSizeOfUsage = size;
        }
    }

    std::string spaces;
    for(unsigned int i = 0; i < maxSizeOfUsage; i++) {
        spaces += ' ';
    }

    // print help message
    stream << "Options:" << std::endl;
    for(auto &option : this->options) {
        unsigned int size = option.getUsageSize();
        stream << "    " << option.optionSymbol
        << (option.hasArg ? Option<OPTION_ENUM>::usageSuffix : "");
        for(unsigned int i = 0; i < maxSizeOfUsage - size; i++) {
            stream << ' ';
        }

        std::vector<std::string> descs(option.getDescriptions());
        unsigned int descsSize = descs.size();
        for(unsigned int i = 0; i < descsSize; i++) {
            if(i > 0) {
                stream << std::endl;
                stream << spaces;
                stream << "    ";
            }
            stream << "    ";
            stream << descs[i];
        }
        stream << std::endl;
    }
}


} // namespace args
} // namespace ydsh

#endif /* MISC_ARGSPARSER_H_ */
