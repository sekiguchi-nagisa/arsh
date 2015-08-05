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

#include <sys/stat.h>

#include <iostream>
#include <fstream>
#include <list>
#include <memory>
#include <unordered_set>

#include <xercesc/sax2/SAX2XMLReader.hpp>
#include <xercesc/sax2/XMLReaderFactory.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/sax2/DefaultHandler.hpp>
#include <xercesc/sax2/Attributes.hpp>

#include <misc/argv.hpp>
#include <misc/debug.h>

using namespace xercesc;

namespace {

class Config {
private:
    /**
     * if true, use system bus.
     * if false, use session bus.
     * default is false(session bus).
     */
    bool systemBus;

    /**
     * if true, search child object.
     * default is false.
     */
    bool recusive;

    /**
     * destination service. must be well known name.
     * default is org.freedesktop.DBus
     */
    std::string dest;

    /**
     * target object path list.
     * must not be empty
     */
    std::vector<std::string> pathList;

    /**
     * introspection xml file.
     * if this is not empty, ignore pathList.
     */
    std::string xmlFileName;

    /**
     * output directory for generated interface file.
     */
    std::string outputDir;

    /**
     * if generated interface files have already existed, overwrite file.
     * default is false
     */
    bool overwrite;

    /**
     * for currently processing object path.
     */
    std::string currentPath;

    /**
     * if true, generate standard interface.
     */
    bool allowStd;

public:
    Config() : systemBus(false), recusive(false),
               dest("org.freedekstop.DBus"), pathList(), xmlFileName(),
               outputDir(), overwrite(false), currentPath(), allowStd(false) {}

    ~Config() = default;

    void setSystemBus(bool systemBus) {
        this->systemBus = systemBus;
    }

    bool isSystemBus() const {
        return this->systemBus;
    }

    void setRecursive(bool recursive) {
        this->recusive = recursive;
    }

    bool isRecursive() const {
        return this->recusive;
    }

    void setDest(const char *dest) {
        this->dest = dest;
    }

    const std::string &getDest() const {
        return this->dest;
    }

    void addPath(const char *path) {
        this->pathList.push_back(path);
    }

    const std::vector<std::string> &getPathList() const {
        return this->pathList;
    }

    void setXmlFileName(const char *fileName) {
        this->xmlFileName = fileName;
    }

    const std::string &getXmlFileName() const {
        return this->xmlFileName;
    }

    void setOutputDir(const char *outputDir) {
        this->outputDir = outputDir;
        if(this->outputDir[this->outputDir.size() - 1] == '/') {
            this->outputDir.erase(this->outputDir.size() - 1);
        }
    }

    const std::string &getOutputDir() const {
        return this->outputDir;
    }

    void setOverwrite(bool overwrite) {
        this->overwrite = overwrite;
    }

    bool isOverwrite() const {
        return this->overwrite;
    }

    void setCurrentPath(std::string &&path) {
        this->currentPath = std::move(path);
    }

    const std::string &getCurrentPath() const {
        return this->currentPath;
    }

    void setAllowStd(bool allowStd) {
        this->allowStd = allowStd;
    }

    bool isAllowStd() const {
        return this->allowStd;
    }
};

#define STR(x) "`" #x "'"
#define check(expr) do { if(!(expr)) { fatal("assertion fail => %s\n", STR(expr)); } } while(0)

static std::string str(const XMLCh *const str) {
    char *ptr = XMLString::transcode(str);
    std::string value(ptr);
    XMLString::release(&ptr);
    return value;
}

static std::string getAttr(const Attributes &attrs, const char *attrName) {
    XMLCh *xch = XMLString::transcode(attrName);
    std::string attr(str(attrs.getValue(xch)));
    XMLString::release(&xch);
    return attr;
}

static bool existFile(const std::string &fileName) {
    struct stat st;
    int result = stat(fileName.c_str(), &st);
    return result == 0;
}

class MethodBuilder {
private:
    std::string methodName;
    std::vector<std::string> args;
    std::vector<std::string> returnTypes;

public:
    MethodBuilder() = default;
    ~MethodBuilder() = default;

    void write(FILE *fp) {
        fprintf(fp, "    function %s(", this->methodName.c_str());

        // write arg
        unsigned int size = this->args.size();
        for(unsigned int i = 0; i < size; i++) {
            if(i > 0) {
                fputs(", ", fp);
            }
            fputs(this->args[i].c_str(), fp);
        }

        fputs(")", fp);

        // write return type
        size = this->returnTypes.size();
        for(unsigned int i = 0; i < size; i++) {
            if(i == 0) {
                fputs(" : ", fp);
            } else {
                fputs(", ", fp);
            }
            fputs(this->returnTypes[i].c_str(), fp);
        }

        fputs("\n", fp);
    }

    void clear() {
        this->methodName.clear();
        this->args.clear();
        this->returnTypes.clear();
    }

    void setMethodName(std::string &&methodName) {
        this->methodName = std::move(methodName);
    }

    void appendArg(std::string &&argName, const std::string &argTypeName) {
        this->args.push_back(std::move(argName));
        this->args.back() += " : ";
        this->args.back() += argTypeName;
    }

    void appendReturnType(const std::string &typeName) {
        this->returnTypes.push_back(typeName);
    }

    bool empty() const {
        return this->methodName.empty() && this->args.empty() && this->returnTypes.empty();
    }
};

class SignalBuilder {
private:
    std::string signalName;
    std::vector<std::string> argTypes;

public:
    SignalBuilder() = default;
    ~SignalBuilder() = default;

    void write(FILE *fp) {
        fprintf(fp, "    function %s($hd : Func<Void", this->signalName.c_str());

        // write arg type
        unsigned int size = this->argTypes.size();
        for(unsigned int i = 0; i < size; i++) {
            if(i == 0) {
                fputs(",[", fp);
            } else {
                fputs(",", fp);
            }
            fputs(this->argTypes[i].c_str(), fp);

            if(i == size - 1) {
                fputs("]", fp);
            }
        }

        fputs(">)\n", fp);
    }

    void clear() {
        this->signalName.clear();
        this->argTypes.clear();
    }

    void setSignalName(std::string &&name) {
        this->signalName = std::move(name);
    }

    void appendArgType(const std::string &type) {
        this->argTypes.push_back(type);
    }

    bool empty() const {
        return this->signalName.empty() && this->argTypes.empty();
    }
};


class IntrospectionDataHandler : public DefaultHandler {
private:
    const Config &config;
    std::list<std::string> &pathList;

    unsigned int nodeCount;
    FILE *fp;
    MethodBuilder mBuilder;
    SignalBuilder sBuilder;
    std::unordered_set<std::string> foundIfaceSet;

public:
    IntrospectionDataHandler(const Config &config, std::list<std::string> &pathList) :
            DefaultHandler(), config(config), pathList(pathList), nodeCount(0), fp(nullptr),
            mBuilder(), sBuilder(), foundIfaceSet() {
        if(!this->config.isAllowStd()) {
            this->generatedPreviously("org.freedesktop.DBus.Peer");
            this->generatedPreviously("org.freedesktop.DBus.Introspectable");
            this->generatedPreviously("org.freedesktop.DBus.Properties");
            this->generatedPreviously("org.freedesktop.DBus.ObjectManager");
        }
    }

    ~IntrospectionDataHandler() = default;

    const Config &getConfig() const {
        return this->config;
    }

    void startElement(const XMLCh * const uri, const XMLCh * const localname,
                      const XMLCh * const qname, const Attributes &attrs);
    void endElement(const XMLCh * const uri, const XMLCh * const localname,
                      const XMLCh * const qname);

private:
    bool generatedPreviously(const std::string &ifaceName) {
        return !this->foundIfaceSet.insert(ifaceName).second;
    }
};

static void decodeImpl(const std::string &desc, unsigned int &index, std::string &out) {
    assert(index < desc.size());

    char ch = desc[index++];
    switch(ch) {
    case 'y':
        out += "Byte";
        break;
    case 'b':
        out += "Boolean";
        break;
    case 'n':
        out += "Int16";
        break;
    case 'q':
        out += "Uint16";
        break;
    case 'i':
        out += "Int32";
        break;
    case 'u':
        out += "Uint32";
        break;
    case 'x':
        out += "Int64";
        break;
    case 't':
        out += "Uint64";
        break;
    case 'd':
        out += "Float";
        break;
    case 's':
        out += "String";
        break;
    case 'o':
        out += "ObjectPath";
        break;
    case 'a': {
        if(index < desc.size() && desc[index] == '{') { // as Map
            out += "Map<";
            index++; // comsume '{'
            decodeImpl(desc, index, out);
            out += ",";
            decodeImpl(desc, index, out);
            index++;    // consume '}'
            out += ">";
        } else {    // as array
            out += "Array<";
            decodeImpl(desc, index, out);
            out += ">";
        }
        break;
    }
    case '(': {
        out += "Tuple<";
        unsigned int count = 0;
        do {
            if(count++ > 0) {
                out += ",";
            }
            decodeImpl(desc, index, out);
        } while(desc[index] != ')');
        out += ">";
        break;
    }
    case 'v':
        out += "Variant";
        break;
    case 'h':
        // currently not supported
    default:
        fatal("unsupported type signature: %c, %s\n", ch, desc.c_str());
        break;
    }
}

static std::string decode(const std::string &desc) {
    std::string str;
    unsigned int index = 0;
    decodeImpl(desc, index, str);
    assert(index == desc.size());
    return str;
}

// ######################################
// ##     IntrospectionDataHandler     ##
// ######################################

void IntrospectionDataHandler::startElement(const XMLCh * const uri, const XMLCh * const localname,
                  const XMLCh * const qname, const Attributes &attrs) {
    std::string elementName(str(localname));
    if(elementName == "node" && this->nodeCount++ != 0) {
        if(this->config.isRecursive()) {
            check(attrs.getLength() == 1);
            std::string nodeName(config.getCurrentPath());
            nodeName += "/";
            nodeName += getAttr(attrs, "name");
            this->pathList.push_back(std::move(nodeName));
        }
    } else if(elementName == "interface") {
        check(attrs.getLength() == 1);
        std::string ifaceName(getAttr(attrs, "name"));

        if(this->generatedPreviously(ifaceName)) {
            this->fp = fopen("/dev/null", "w");
            if(this->fp == nullptr) {
                std::cerr << "cannot open file: /dev/null" << strerror(errno) << std::endl;
                exit(1);
            }
        } else if(this->config.getOutputDir().empty()) {
            this->fp = stdout;
        } else {
            std::string fileName(this->config.getOutputDir());
            fileName += '/';
            fileName += ifaceName;

            // open target file
            const char *fileNameStr = fileName.c_str();
            if(!this->config.isOverwrite() && existFile(fileName)) {    // check file existence
                fileNameStr = "/dev/null";
            }
            this->fp = fopen(fileNameStr, "w");
            if(this->fp == nullptr) {
                std::cerr << "cannot open file: " << fileNameStr << strerror(errno) << std::endl;
                exit(1);
            }
        }
        // write
        fprintf(this->fp, "interface %s {\n", ifaceName.c_str());
    } else if(elementName == "method") {
        check(attrs.getLength() == 1);
        std::string methodName(getAttr(attrs, "name"));
        this->mBuilder.setMethodName(std::move(methodName));
    } else if(elementName == "signal") {
        check(attrs.getLength() == 1);
        std::string signalName(getAttr(attrs, "name"));
        this->sBuilder.setSignalName(std::move(signalName));
    } else if(elementName == "property") {
        std::string name(getAttr(attrs, "name"));
        std::string type(getAttr(attrs, "type"));
        std::string access(getAttr(attrs, "access"));

        if(access.find("write") != std::string::npos) {
            fprintf(this->fp, "    var ");
        } else {
            fprintf(this->fp, "    let ");
        }
        fputs(name.c_str(), this->fp);
        fputs(" : ", this->fp);

        fputs(decode(type).c_str(), this->fp);
        fputs("\n", this->fp);
    } else if(elementName == "arg") {
        std::string t(getAttr(attrs, "type"));
        std::string type = decode(t);

        if(!this->mBuilder.empty()) {
            std::string direction(getAttr(attrs, "direction"));
            if(direction == "in") {
                std::string argName("$");
                argName += getAttr(attrs, "name");
                this->mBuilder.appendArg(std::move(argName), type);
            } else {
                this->mBuilder.appendReturnType(type);
            }
        } else if(!this->sBuilder.empty()) {
            this->sBuilder.appendArgType(type);
        } else {
            fatal("broken arg\n");
        }
    }
}

void IntrospectionDataHandler::endElement(const XMLCh * const uri, const XMLCh * const localname,
                const XMLCh * const qname) {
    std::string elementName(str(localname));
    if(elementName == "node") {
        this->nodeCount--;
    } else if(elementName == "interface") {
        fprintf(this->fp, "}\n\n");
        fflush(this->fp);
        if(!this->config.getOutputDir().empty()) {
            fclose(this->fp);
        }
    } else if(elementName == "method") {
        this->mBuilder.write(this->fp);
        this->mBuilder.clear();
    } else if(elementName == "signal") {
        this->sBuilder.write(this->fp);
        this->sBuilder.clear();
    }
}


static std::string introspect(const Config &config) {
    std::string cmd("dbus-send --print-reply=literal");
    cmd += (config.isSystemBus() ? " --system" : " --session");
    cmd += " --dest=";
    cmd += config.getDest();
    cmd += " ";
    cmd += config.getCurrentPath();
    cmd += " org.freedesktop.DBus.Introspectable.Introspect";

    FILE *fp = popen(cmd.c_str(), "r");
    if(fp == nullptr) {
        perror("dbus-send execution failed\n");
        exit(1);
    }

    std::string msg;

    const unsigned int size = 256;
    char buf[size + 1];
    int readSize;
    while((readSize = fread(buf, sizeof(char), size, fp)) > 0) {
        buf[readSize] = '\0';
        msg += buf;
    }

    pclose(fp);

    return msg;
}

static void parse(std::unique_ptr<SAX2XMLReader> &parser, const std::string &xmlStr) {
    try {
        MemBufInputSource input((const XMLByte*) xmlStr.c_str(), xmlStr.size(), "source");
        parser->parse(input);
    } catch(const XMLException &e) {
        char *msg = XMLString::transcode(e.getMessage());
        std::cerr << msg << std::endl;
        XMLString::release(&msg);
        exit(1);
    }
}

static void generateIface(Config &config) {
    std::list<std::string> pathList;
    IntrospectionDataHandler handler(config, pathList);

    std::unique_ptr<SAX2XMLReader> parser(XMLReaderFactory::createXMLReader());
    parser->setContentHandler(&handler);

    // for specifed xml file
    if(!config.getXmlFileName().empty()) {
        FILE *fp = fopen(config.getXmlFileName().c_str(), "r");
        if(fp == nullptr) {
            fprintf(stderr, "%s: %s\n", config.getXmlFileName().c_str(), strerror(errno));
            exit(1);
        }
        unsigned int size = 256;
        char buf[size + 1];
        int readSize;
        std::string xmlStr;
        while((readSize = fread(buf, sizeof(char), size, fp)) > 0) {
            buf[readSize] = '\0';
            xmlStr += buf;
        }
        if(xmlStr.empty()) {
            fprintf(stderr, "broken xml\n");
            exit(1);
        }
        parse(parser, xmlStr);
        return;
    }


    // init path list
    for(auto &e : config.getPathList()) {
        pathList.push_back(e);
    }

    while(!pathList.empty()) {
        config.setCurrentPath(std::move(pathList.front()));
        pathList.pop_front();

        std::string xmlStr(introspect(config));
        if(xmlStr.empty()) {
            fprintf(stderr, "broken xml\n");
            exit(1);
        }
        parse(parser, xmlStr);
    }
}

} // namespace

#define EACH_OPT(OP) \
    OP(SYSTEM_BUS,  "--system",    0,           "use system bus") \
    OP(SESSION_BUS, "--session",   0,           "use session bus (default)") \
    OP(RECURSIVE,   "--recursive", 0,           "search interface from child object") \
    OP(DEST,        "--dest",      REQUIRE_ARG, "destination service name (must be well-known name)") \
    OP(OUTPUT,      "--output",    REQUIRE_ARG, "specify output directory (default is standard output)") \
    OP(OVERWRITE,   "--overwrite", 0,           "if generated interface files have already existed, overwrite them") \
    OP(HELP,        "--help",      0,           "show this help message") \
    OP(XML_FILE,    "--xml",       REQUIRE_ARG | IGNORE_REST, "generate interface from specified xml. no introspection.(ignore some option)") \
    OP(ALLOW_STD,   "--allow-std", 0,           "allow standard D-Bus interface generation")

enum class OptionSet : unsigned int {
#define GEN_ENUM(E, S, F, D) E,
    EACH_OPT(GEN_ENUM)
#undef GEN_ENUM
};

int main(int argc, char **argv) {
    using namespace ydsh::argv;

    static const Option<OptionSet> options[] = {
#define GEN_OPT(E, S, F, D) {OptionSet::E, S, (F), D},
            EACH_OPT(GEN_OPT)
#undef GEN_OPT
    };

    std::vector<std::pair<OptionSet, const char *>> cmdLines;
    int restIndex = argc;
    try {
        restIndex = ydsh::argv::parseArgv(argc, argv, options, cmdLines);
    } catch(const ParseError &e) {
        std::cerr << e.getMessage() << std::endl;
        std::cerr << options << std::endl;
        return 1;
    }

    // set option
    Config config;
    bool foundDest = false;
    bool foundXml = false;

    for(auto &cmdLine : cmdLines) {
        switch(cmdLine.first) {
        case OptionSet::SYSTEM_BUS:
            config.setSystemBus(true);
            break;
        case OptionSet::SESSION_BUS:
            config.setSystemBus(false);
            break;
        case OptionSet::RECURSIVE:
            config.setRecursive(true);
            break;
        case OptionSet::DEST:
            config.setDest(cmdLine.second);
            foundDest = true;
            break;
        case OptionSet::OUTPUT:
            config.setOutputDir(cmdLine.second);
            break;
        case OptionSet::OVERWRITE:
            config.setRecursive(true);
            break;
        case OptionSet::HELP:
            std::cout << options << std::endl;
            return 0;
        case OptionSet::XML_FILE:
            config.setXmlFileName(cmdLine.second);
            foundXml = true;
            break;
        case OptionSet::ALLOW_STD:
            config.setAllowStd(true);
            break;
        }
    }

    if(!foundDest && !foundXml) {
        std::cerr << "require --dest [destination name]" << std::endl;
        std::cout << options << std::endl;
        return 1;
    }

    const int size = argc - restIndex;
    if(size == 0 && !foundXml) {
        std::cerr << "require atleast one object path" << std::endl;
        std::cout << options << std::endl;
        return 1;
    }
    
    for(int i = restIndex; i < argc; i++) {
        config.addPath(argv[i]);
    }

    // init xerces
    try {
        XMLPlatformUtils::Initialize();
    } catch(const XMLException &e) {
        char *msg = XMLString::transcode(e.getMessage());
        std::cerr << msg << std::endl;
        XMLString::release(&msg);
        exit(1);
    }

    generateIface(config);

    XMLPlatformUtils::Terminate();

    return 0;
}