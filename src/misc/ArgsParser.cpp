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

#include <misc/ArgsParser.h>

namespace args {

// ########################
// ##     ParseError     ##
// ########################

ParseError::ParseError(const char *message, const char *suffix) :
        message(message), suffix(suffix) {
}

ParseError::~ParseError() {
}

const char *ParseError::getMessage() const {
    return this->message;
}

const char *ParseError::getSuffix() const {
    return this->suffix;
}

// ####################
// ##     Option     ##
// ####################

unsigned int ArgsParser::Option::getUsageSize() const {
    return strlen(this->optionSymbol) + (this->hasArg ? strlen(" <arg>") : 0);
}

std::vector<std::string> ArgsParser::Option::getDescriptions() const {
    std::vector<std::string> bufs;
    std::string buf;
    for(unsigned int i = 0; this->description[i] != '\0'; i++) {
        char ch = this->description[i];
        if(ch == '\n') {
            if(!buf.empty()) {
                bufs.push_back(buf);
                buf.clear();
            }
        } else {
            buf += ch;
        }
    }
    if(!buf.empty()) {
        bufs.push_back(buf);
    }
    return bufs;
}

const char *ArgsParser::Option::usageSuffix = " <arg>";

// ########################
// ##     ArgsParser     ##
// ########################

ArgsParser::ArgsParser() :
        options() {
}

ArgsParser::~ArgsParser() {
}

ArgsParser *ArgsParser::addOption(unsigned int optionId, const char *optionSymbol,
        bool hasArg, const char *description) {
    this->options.push_back({optionId, optionSymbol, hasArg, description});
    return this;
}

std::vector<const char *> ArgsParser::parse(int argc, char **argv,
        std::vector<std::pair<unsigned int, const char*>> &cmdLines) throw(ParseError) {
    static char empty[] = "";
    std::vector<const char *> restArgs;

    for(int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        bool findOption = false;
        for(const Option &option : this->options) {
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

void ArgsParser::printHelp(std::ostream &stream) {
    unsigned int maxSizeOfUsage = 0;

    // compute usage size
    for(const Option &option : this->options) {
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
    for(const Option &option : this->options) {
        unsigned int size = option.getUsageSize();
        stream << "    " << option.optionSymbol
                << (option.hasArg ? Option::usageSuffix : "");
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

}
