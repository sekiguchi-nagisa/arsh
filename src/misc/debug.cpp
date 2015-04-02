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

#include <stdio.h>
#include <stdarg.h>
#include <string>

#define DEST_TYPE_ERR    "stderr"
#define DEST_TYPE_SYSLOG "syslog"

#ifndef LOG_DEST
#define LOG_DEST DEST_TYPE_ERR
#endif

class Logger {
private:
    FILE *fp;

    Logger();

public:
    static Logger getInstance();

    ~Logger();
    FILE *getfp();
};

Logger::Logger() : fp(0) {
    std::string logDest(LOG_DEST);
    if(logDest == DEST_TYPE_ERR) {
        this->fp = 0;
    } else if(logDest == DEST_TYPE_SYSLOG) {
        //FIXME:
    } else {
        this->fp = fopen(logDest.c_str(), "w");
    }
    fprintf(stderr, "initialize logger destination: %s\n", LOG_DEST);
}

Logger Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if(this->fp != 0) {
        fclose(this->fp);
    }
}

FILE *Logger::getfp() {
    return this->fp != 0 ? fp : stderr;
}

void debug_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(Logger::getInstance().getfp(), format, args);
    va_end(args);
}

