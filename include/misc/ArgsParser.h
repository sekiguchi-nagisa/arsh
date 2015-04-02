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

namespace args {

class ParseError {
private:
    /**
     * not delete it.
     */
    const char *message;

    /**
     * message suffix.
     * not delete it.
     */
    const char *suffix;

public:
    ParseError(const char *message, const char *suffix);
    ~ParseError();

    const char *getMessage() const;
    const char *getSuffix() const;
};

class ArgsParser {
private:
    struct Option {
        unsigned int optionId;
        const char *optionSymbol;
        bool hasArg;
        const char *description;

        unsigned int getUsageSize() const;
        std::vector<std::string> getDescriptions() const;

        static const char *usageSuffix;
    };

    std::vector<Option> options;

public:
    ArgsParser();
    ~ArgsParser();

    ArgsParser *addOption(unsigned int optionId, const char *optionSymbol,
            bool hasArg = false, const char *description = "");

    /**
     * write parsed options to cmdLines.
     * if has rest argument, write to restArgs.
     * if parse success, return rest arguments
     */
    std::vector<const char *> parse(int argc, char **argv,
            std::vector<std::pair<unsigned int, const char*>> &cmdLines) throw(ParseError);

    void printHelp(std::ostream &stream);
};

}

#endif /* MISC_ARGSPARSER_H_ */
