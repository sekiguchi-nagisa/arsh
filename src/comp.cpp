/*
 * Copyright (C) 2019 Nagisa Sekiguchi
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

#include <dirent.h>
#include <pwd.h>

#include <vector>

#include "vm.h"
#include "parser.h"
#include "logger.h"
#include "misc/num_util.hpp"
#include "misc/files.h"

extern char **environ;  //NOLINT

namespace ydsh {

// for input completion
enum class EscapeOp {
    NOP,
    COMMAND_NAME,
    COMMAND_NAME_PART,
    COMMAND_ARG,
};

static std::string escape(const char *str, EscapeOp op) {
    std::string buf;
    if(op == EscapeOp::NOP) {
        buf += str;
        return buf;
    }

    if(op == EscapeOp::COMMAND_NAME) {
        int ch = *str;
        if(isDecimal(ch) || ch == '+' || ch == '-' || ch == '[' || ch == ']') {
            buf += '\\';
            buf += static_cast<char>(ch);
            str++;
        }
    }

    while(*str != '\0') {
        int ch = *(str++);
        bool found = false;
        switch(ch) {
        case ' ':
        case '\\':
        case ';':
        case '\'':
        case '"':
        case '`':
        case '|':
        case '&':
        case '<':
        case '>':
        case '(':
        case ')':
        case '$':
        case '#':
        case '~':
            found = true;
            break;
        case '{':
        case '}':
            if(op == EscapeOp::COMMAND_NAME || op == EscapeOp::COMMAND_NAME_PART) {
                found = true;
            }
            break;
        default:
            if((ch >= 0 && ch < 32) || ch == 127) { // escape unprintable character
                char d[32];
                snprintf(d, arraySize(d), "$'\\x%02x'", ch);
                buf += d;
                continue;
            }
            break;
        }

        if(found) {
            buf += '\\';
        }
        buf += static_cast<char>(ch);
    }
    return buf;
}


static void append(ArrayObject &can, const char *str, EscapeOp op) {
    assert(can.getTypeID() == static_cast<unsigned int>(TYPE::StringArray));
    std::string estr = escape(str, op);
    can.append(DSValue::createStr(std::move(estr)));
}

static void append(ArrayObject &buf, const std::string &str, EscapeOp op) {
    append(buf, str.c_str(), op);
}

static bool startsWith(const char *s1, const char *s2) {
    return s1 != nullptr && s2 != nullptr && strstr(s1, s2) == s1;
}

class Completer {
private:
    const char *name;

protected:
    explicit Completer(const char *value) : name(value) {}

    void setName(const char *v) {
        this->name = v;
    }

public:
    virtual void operator()(ArrayObject &ret) = 0;

    /**
     * for debugging.
     * @return
     */
    const char *getName() const {
        return this->name;
    }

    virtual ~Completer() = default;
};

static bool isKeyword(const char *value) {
    return *value != '<' || *(value + strlen(value) - 1) != '>';
}

class ExpectedTokenCompleter : public Completer {
private:
    const char *token;
    const std::vector<TokenKind> *tokens;

    ExpectedTokenCompleter(const char *token, const std::vector<TokenKind> *tokens) :
            Completer("Expected"), token(token), tokens(tokens) {}

public:
    explicit ExpectedTokenCompleter(const char *token) : ExpectedTokenCompleter(token, nullptr) {}

    explicit ExpectedTokenCompleter(const std::vector<TokenKind > &tokens) :
            ExpectedTokenCompleter(nullptr, &tokens) {}

    void operator()(ArrayObject &results) override {
        if(this->token) {
            append(results, this->token, EscapeOp::NOP);
        }
        if(this->tokens) {
            for(auto &e : *this->tokens) {
                const char *value = toString(e);
                if(isKeyword(value)) {
                    append(results, value, EscapeOp::NOP);
                }
            }
        }
    }
};

static bool isStmtKeyword(TokenKind kind) {
    switch(kind) {
#define GEN_CASE(T) case T:
    EACH_LA_statement(GEN_CASE)
        return true;
#undef GEN_CASE
    default:
        return false;
    }
}

static bool isExprKeyword(TokenKind kind) {
    switch(kind) {
#define GEN_CASE(T) case T:
    EACH_LA_expression(GEN_CASE)
        return true;
#undef GEN_CASE
    default:
        return false;
    }
}

class KeywordCompleter : public Completer {
private:
    const std::string token;

    /**
     * if true, only complete expression keyword,
     * otherwise complete statement keyword
     */
    bool onlyExpr;

public:
    KeywordCompleter(std::string &&value, bool expr) :
            Completer("Keyword"), token(std::move(value)), onlyExpr(expr) {}

    void operator()(ArrayObject &results) override {
        TokenKind table[] = {
#define GEN_ITEM(T) T,
            EACH_LA_statement(GEN_ITEM)
#undef GEN_ITEM
        };
        for(auto &e : table) {
            if(this->onlyExpr && !isExprKeyword(e)) {
                continue;
            }
            const char *value = toString(e);
            if(isKeyword(value) && startsWith(value, this->token.c_str())) {
                append(results, value, EscapeOp::NOP);
            }
        }
    }
};

class EnvNameCompleter : public Completer {
private:
    const std::string envName;

public:
    explicit EnvNameCompleter(std::string &&name) : Completer("Env"), envName(std::move(name)) {}

    void operator()(ArrayObject &results) override {
        for(unsigned int i = 0; environ[i] != nullptr; i++) {
            const char *env = environ[i];
            if(startsWith(env, this->envName.c_str())) {
                const char *ptr = strchr(env, '=');
                assert(ptr != nullptr);
                append(results, std::string(env, ptr - env), EscapeOp::NOP);
            }
        }
    }
};

class CmdNameCompleter : public Completer {
private:
    const SymbolTable &symbolTable;
    const std::string token;

public:
    /**
     *
     * @param symbolTable
     * @param token
     * may be empty string
     */
    CmdNameCompleter(const SymbolTable &symbolTable, std::string &&token) :
            Completer("Command"), symbolTable(symbolTable), token(std::move(token)) {}

    void operator()(ArrayObject &results) override;
};

static std::vector<std::string> computePathList(const char *pathVal) {
    std::vector<std::string> result;
    result.emplace_back();
    assert(pathVal != nullptr);

    for(unsigned int i = 0; pathVal[i] != '\0'; i++) {
        int ch = pathVal[i];
        if(ch == ':') {
            result.emplace_back();
        } else {
            result.back() += static_cast<char>(ch);
        }
    }

    // expand tilde
    for(auto &s : result) {
        expandTilde(s);
    }

    return result;
}

void CmdNameCompleter::operator()(ArrayObject &results) {
    // search user defined command
    for(const auto &iter : this->symbolTable.globalScope()) {
        const char *name = iter.first.c_str();
        if(startsWith(name, CMD_SYMBOL_PREFIX)) {
            name += strlen(CMD_SYMBOL_PREFIX);
            if(startsWith(name, this->token.c_str())) {
                append(results, name, EscapeOp::COMMAND_NAME);
            }
        }
    }

    // search builtin command
    const unsigned int bsize = getBuiltinCommandSize();
    for(unsigned int i = 0; i < bsize; i++) {
        const char *name = getBuiltinCommandName(i);
        if(startsWith(name, this->token.c_str())) {
            append(results, name, EscapeOp::COMMAND_NAME);
        }
    }


    // search external command
    const char *path = getenv(ENV_PATH);
    if(path == nullptr) {
        return;
    }

    auto pathList(computePathList(path));
    for(const auto &p : pathList) {
        DIR *dir = opendir(p.c_str());
        if(dir == nullptr) {
            continue;
        }

        for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
            const char *name = entry->d_name;
            if(startsWith(name, this->token.c_str())) {
                std::string fullpath(p);
                fullpath += '/';
                fullpath += name;
                if(S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) == 0) {
                    append(results, name, EscapeOp::COMMAND_NAME);
                }
            }
        }
        closedir(dir);
    }
}

class GlobalVarNameCompleter : public Completer {
private:
    const SymbolTable &symbolTable;

    const std::string token;

public:
    GlobalVarNameCompleter(const SymbolTable &symbolTable, std::string &&token) :
            Completer("GlobalVar"), symbolTable(symbolTable), token(std::move(token)) {}

    void operator()(ArrayObject &results) override {
        for(const auto &iter : this->symbolTable.globalScope()) {
            const char *varName = iter.first.c_str();
            if(!this->token.empty() && !startsWith(varName, CMD_SYMBOL_PREFIX)
               && !startsWith(varName, MOD_SYMBOL_PREFIX)
               && startsWith(varName, this->token.c_str() + 1)) {
                append(results, iter.first, EscapeOp::NOP);
            }
        }
    }
};

enum class FileNameCompOp : unsigned int {
    ONLY_EXEC = 1 << 0,
    TIDLE     = 1 << 1,
};

template <> struct allow_enum_bitop<FileNameCompOp> : std::true_type {};

class FileNameCompleter : public Completer {
protected:
    const char *baseDir;

    const std::string token;

    const FileNameCompOp op;

public:
    FileNameCompleter(const char *baseDir, std::string &&token, FileNameCompOp op = FileNameCompOp()) :
            Completer(""), baseDir(baseDir), token(std::move(token)), op(op) {
        this->setName(this->onlyExec() ? "QualifiedCommand" : "FileName");
    }

protected:
    bool onlyExec() const {
        return hasFlag(this->op, FileNameCompOp::ONLY_EXEC);
    }

    bool tilde() const {
        return hasFlag(this->op, FileNameCompOp::TIDLE);
    }

public:
    void operator()(ArrayObject &results) override;
};

void FileNameCompleter::operator()(ArrayObject &results) {
    const auto s = this->token.find_last_of('/');

    // complete tilde
    if(this->token[0] == '~' && s == std::string::npos && this->tilde()) {
        setpwent();
        for(struct passwd *entry = getpwent(); entry != nullptr; entry = getpwent()) {
            if(startsWith(entry->pw_name, this->token.c_str() + 1)) {
                std::string name("~");
                name += entry->pw_name;
                name += '/';
                append(results, name, EscapeOp::NOP);
            }
        }
        endpwent();
        return;
    }

    // complete file name

    /**
     * resolve directory path
     */
    std::string targetDir;
    if(s == 0) {
        targetDir = "/";
    } else if(s != std::string::npos) {
        targetDir = this->token.substr(0, s);
        if(this->tilde()) {
            expandTilde(targetDir);
        }
        targetDir = expandDots(this->baseDir, targetDir.c_str());
    } else {
        targetDir = expandDots(this->baseDir, ".");
    }
    LOG(DUMP_CONSOLE, "targetDir = %s", targetDir.c_str());

    /**
     * resolve name
     */
    std::string name;
    if(s != std::string::npos) {
        name = this->token.substr(s + 1);
    } else {
        name = this->token;
    }

    DIR *dir = opendir(targetDir.c_str());
    if(dir == nullptr) {
        return;
    }

    for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
        if(startsWith(entry->d_name, name.c_str())) {
            if(name.empty() && (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)) {
                continue;
            }

            std::string fullpath(targetDir);
            fullpath += '/';
            fullpath += entry->d_name;

            if(this->onlyExec() && S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) != 0) {
                continue;
            }

            std::string fileName = entry->d_name;
            if(S_ISDIR(getStMode(fullpath.c_str()))) {
                fileName += '/';
            }
            append(results, fileName, this->onlyExec() ? EscapeOp::COMMAND_NAME_PART : EscapeOp::COMMAND_ARG);
        }
    }
    closedir(dir);
}

struct ModNameCompleter : public FileNameCompleter {
    ModNameCompleter(const char *scriptDir, std::string &&token, bool tilde) :
            FileNameCompleter(scriptDir, std::move(token), tilde ? FileNameCompOp::TIDLE : FileNameCompOp{}) {
        this->setName("Module");
    }

    void operator()(ArrayObject &results) override {
        // complete in SCRIPT_DIR
        FileNameCompleter::operator()(results);

        // complete in local module dir
        std::string localModDir = LOCAL_MOD_DIR;
        expandTilde(localModDir);
        this->baseDir = localModDir.c_str();
        FileNameCompleter::operator()(results);

        // complete in system module dir
        this->baseDir = SYSTEM_MOD_DIR;
        FileNameCompleter::operator()(results);
    }
};

class AndCompleter : public Completer {
private:
    std::unique_ptr<Completer> first;

    std::unique_ptr<Completer> second;

public:
    AndCompleter(std::unique_ptr<Completer> &&first, std::unique_ptr<Completer> &&second) :
            Completer("And"), first(std::move(first)), second(std::move(second)) {}

    void operator()(ArrayObject &results) override {
        (*this->first)(results);
        (*this->second)(results);
    }
};

class OrCompleter : public Completer {
private:
    std::unique_ptr<Completer> first;

    std::unique_ptr<Completer> second;

public:
    OrCompleter(std::unique_ptr<Completer> &&first, std::unique_ptr<Completer> &&second) :
            Completer("Or"), first(std::move(first)), second(std::move(second)) {}

    void operator()(ArrayObject &results) override {
        (*this->first)(results);
        if(results.getValues().empty()) {
            (*this->second)(results);
        }
    }
};

class UserDefinedCompleter : public Completer {
private:
    DSState &state;

    DSValue hook;   // must be FuncObject

    DSValue tokens; // must Array<String>

    const unsigned int tokenIndex;

public:
    UserDefinedCompleter(DSState &state, DSValue &&hook, DSValue &&tokens, unsigned int tokenIndex) :
            Completer("UserDefined"), state(state),
            hook(std::move(hook)), tokens(std::move(tokens)), tokenIndex(tokenIndex) {}

    void operator()(ArrayObject &ret) override {
        auto result = callFunction(this->state, std::move(this->hook),
                makeArgs(std::move(this->tokens), DSValue::createInt(this->tokenIndex)));
        if(this->state.hasError()) {
            return;
        }

        for(auto &e : typeAs<ArrayObject>(result)->getValues()) {
            append(ret, str(e), EscapeOp::COMMAND_ARG);
        }
    }
};

static bool isRedirOp(TokenKind kind) {
    switch(kind) {
#define GEN_CASE(T) case T:
    EACH_LA_redirFile(GEN_CASE)
        return true;
#undef GEN_CASE
    default:
        return false;
    }
}

/**
 * if found command, return CmdNode
 * @param node
 * @return
 */
static const CmdNode *inCmdMode(const Node &node) {
    switch(node.getNodeKind()) {
    case NodeKind::UnaryOp:
        return inCmdMode(*static_cast<const UnaryOpNode &>(node).getExprNode());
    case NodeKind::BinaryOp:
        return inCmdMode(*static_cast<const BinaryOpNode &>(node).getRightNode());
    case NodeKind::Cmd:
        return static_cast<const CmdNode *>(&node);
    case NodeKind::Pipeline:
        return inCmdMode(*static_cast<const PipelineNode &>(node).getNodes().back());
    case NodeKind::Fork: {
        auto &forkNode = static_cast<const ForkNode &>(node);
        return forkNode.getOpKind() == ForkKind::COPROC ? inCmdMode(forkNode.getExprNode()) : nullptr;
    }
    case NodeKind::Assert: {
        auto &assertNode = static_cast<const AssertNode &>(node);
        return assertNode.getMessageNode()->getToken().size == 0 ? inCmdMode(*assertNode.getCondNode()) : nullptr;
    }
    case NodeKind::Jump: {
        auto &jumpNode = static_cast<const JumpNode &>(node);
        return jumpNode.getOpKind() != JumpNode::CONTINUE_ ? inCmdMode(jumpNode.getExprNode()) : nullptr;
    }
    case NodeKind::VarDecl: {
        auto &vardeclNode = static_cast<const VarDeclNode &>(node);
        return vardeclNode.getExprNode() != nullptr ? inCmdMode(*vardeclNode.getExprNode()) : nullptr;
    }
    case NodeKind::Assign:
        return inCmdMode(*static_cast<const AssignNode &>(node).getRightNode());
    default:
        break;
    }
    return nullptr;
}

class CompleterFactory { //TODO: type aware completion
private:
    DSState &state;

    const unsigned int cursor;

    Lexer lexer;

    Parser parser;

    TokenTracker tracker;

    std::unique_ptr<Node> node; // if has error, is null

    enum class CompType {
        NONE,
        CUR,
    };

public:
    CompleterFactory(DSState &st, const char *data, unsigned int size);

private:
    std::unique_ptr<Completer> createModNameCompleter(CompType type) const {
        if(type == CompType::NONE) {
            return std::make_unique<ModNameCompleter>(this->state.getScriptDir(), "", false);
        }
        auto token = this->curToken();
        bool tilde = this->lexer.startsWith(token, '~');
        return std::make_unique<ModNameCompleter>(this->state.getScriptDir(), this->lexer.toCmdArg(token), tilde);
    }

    std::unique_ptr<Completer> createFileNameCompleter(CompType type) const {
        if(type == CompType::NONE) {
            return std::make_unique<FileNameCompleter>(this->state.logicalWorkingDir.c_str(), "");
        }

        FileNameCompOp op{};
        auto token = this->curToken();
        if(this->lexer.startsWith(token, '~')) {
            setFlag(op, FileNameCompOp::TIDLE);
        }
        return std::make_unique<FileNameCompleter>(this->state.logicalWorkingDir.c_str(), this->lexer.toCmdArg(token), op);
    }

    std::unique_ptr<Completer> createCmdNameCompleter(CompType type) const {
        if(type == CompType::NONE) {
            return std::make_unique<CmdNameCompleter>(this->state.symbolTable, "");
        }

        auto token = this->curToken();
        auto arg = this->lexer.toCmdArg(token);
        bool tilde = this->lexer.startsWith(token, '~');
        bool isDir = strchr(arg.c_str(), '/') != nullptr;
        if(tilde || isDir) {
            auto op = FileNameCompOp::ONLY_EXEC;
            if(tilde) {
                setFlag(op, FileNameCompOp::TIDLE);
            }
            return std::make_unique<FileNameCompleter>(this->state.logicalWorkingDir.c_str(), std::move(arg), op);
        }
        return std::make_unique<CmdNameCompleter>(this->state.symbolTable, std::move(arg));
    }

    std::unique_ptr<Completer> createGlobalVarNameCompleter(Token token) const {
        return std::make_unique<GlobalVarNameCompleter>(this->state.symbolTable, this->lexer.toTokenText(token));
    }

    std::unique_ptr<Completer> createEnvNameCompleter(CompType type) const {
        return std::make_unique<EnvNameCompleter>(type == CompType::CUR ? this->lexer.toTokenText(this->curToken()) : "");
    }

    std::unique_ptr<Completer> createExpectedTokenCompleter() const {
        assert(this->parser.hasError());
        return std::make_unique<ExpectedTokenCompleter>(this->parser.getError().getExpectedTokens());
    }

    std::unique_ptr<Completer> createKeywordCompleter(CompType type, bool onlyExpr) const {
        return std::make_unique<KeywordCompleter>(
                type == CompType::CUR ? this->lexer.toTokenText(this->curToken()) : "", onlyExpr);
    }

    static std::unique_ptr<Completer> createAndCompleter(std::unique_ptr<Completer> &&first,
                                                    std::unique_ptr<Completer> &&second) {
        if(!first) {
            return std::move(second);
        }
        if(!second) {
            return std::move(first);
        }
        return std::make_unique<AndCompleter>(std::move(first), std::move(second));
    }

    std::unique_ptr<Completer> createCmdArgCompleter(CompType type) const {
        auto comp = this->createFileNameCompleter(type);
        auto udc = this->createUserDefinedCompleter();
        if(udc) {
            comp = std::make_unique<OrCompleter>(std::move(udc), std::move(comp));
        }
        return comp;
    }

    std::unique_ptr<Completer> createUserDefinedCompleter() const;

    std::vector<DSValue> resolveCmdTokens() const;

    std::unique_ptr<Node> applyAndGetLatest() {
        std::unique_ptr<Node> lastNode;
        while(this->parser) {
            lastNode = this->parser();
            if(this->parser.hasError()) {
                break;
            }
        }
        return lastNode;
    }

    TokenKind curKind() const {
        return this->tracker.getTokenPairs().back().first;
    }

    Token curToken() const {
        return this->tracker.getTokenPairs().back().second;
    }

    TokenKind errorTokenKind() const {
        assert(this->parser.hasError());
        return this->parser.getError().getTokenKind();
    }

    Token errorToken() const {
        assert(this->parser.hasError());
        return this->parser.getError().getErrorToken();
    }

    bool isErrorKind(const char *value) const {
        return strcmp(this->parser.getError().getErrorKind(), value) == 0;
    }

    bool inTyping() const {
        return this->inTyping(this->curToken());
    }

    bool inTyping(Token token) const {
        return token.pos + token.size == this->cursor;
    }

    bool afterTyping() const {
        auto token = this->curToken();
        return token.pos + token.size < this->cursor;
    }

    bool inCommand() const {
        return inCmdMode(*this->node) != nullptr;
    }

    bool inCmdStmt() const {    //FIXME: check lexer mode in error
        return this->node && this->inCommand() && this->node->is(NodeKind::Cmd);
    }

    bool requireSingleCmdArg() const {
        if(this->inTyping()) {
            if(this->node->is(NodeKind::With)) {
                return true;
            }
        }
        return false;
    }

    bool requireMod() const {
        if(this->inTyping()) {
            if(this->node->is(NodeKind::Source)) {
                return static_cast<const SourceNode&>(*this->node).getName().empty();
            }
        }
        return false;
    }

    bool foundExpected(TokenKind kind) const {
        assert(this->parser.hasError());
        for(auto &value : this->parser.getError().getExpectedTokens()) {
            if(value == kind) {
                return true;
            }
        }
        return false;
    }

    DSValue newStrObj(Token token) const {
        std::string value = this->lexer.toTokenText(token);
        return DSValue::createStr(std::move(value));
    }

    std::unique_ptr<Completer> selectWithCmd(bool exactly = false) const;

    std::unique_ptr<Completer> selectCompleter() const;

public:
    std::unique_ptr<Completer> operator()() const {
        auto ret = this->selectCompleter();

        LOG_EXPR(DUMP_CONSOLE, [&]{
            std::string str = "token size: ";
            str += std::to_string(this->tracker.getTokenPairs().size());
            str += "\n";
            for(auto &t : this->tracker.getTokenPairs()) {
                str += "kind: ";
                str += toString(t.first);
                str += ", token: ";
                str += toString(t.second);
                str += ", text: ";
                str += this->lexer.toTokenText(t.second);
                str += "\n";
            }
            str += "ckind: ";
            str += (ret ? ret->getName() : "None");
            return str;
        });

        return ret;
    }
};

// ##############################
// ##     CompleterFactory     ##
// ##############################

CompleterFactory::CompleterFactory(DSState &st, const char *data, unsigned int size) :
        state(st), cursor(size),
        lexer("<line>", data, size), parser(this->lexer) {
    LOG(DUMP_CONSOLE, "line: %s, cursor: %u", std::string(data, size).c_str(), size);
    this->parser.setTracker(&this->tracker);
    this->node = this->applyAndGetLatest();
}

std::unique_ptr<Completer> CompleterFactory::createUserDefinedCompleter() const {
    auto hook = getGlobal(this->state, VAR_COMP_HOOK);
    if(hook.isInvalid()) {
        return nullptr;
    }
    if(this->parser.hasError()) {  //FIXME
        return nullptr;
    }

    auto tokens = this->resolveCmdTokens();
    unsigned int index = tokens.size();
    if(this->inTyping()) {
        index--;
    }
    auto obj = DSValue::create<ArrayObject>(this->state.symbolTable.get(TYPE::StringArray), std::move(tokens));
    return std::make_unique<UserDefinedCompleter>(this->state, std::move(hook), std::move(obj), index);
}

std::vector<DSValue> CompleterFactory::resolveCmdTokens() const {
    assert(this->node); //FIXME
    std::vector<DSValue> tokens;
    auto *cmdNode = inCmdMode(*this->node);
    tokens.push_back(this->newStrObj(cmdNode->getNameNode().getToken()));
    for(auto &argNode : cmdNode->getArgNodes()) {
        if(argNode->is(NodeKind::Redir)) {
            continue;
        }
        tokens.push_back(this->newStrObj(argNode->getToken()));
    }
    return tokens;
}

std::unique_ptr<Completer> CompleterFactory::selectWithCmd(bool exactly) const {
    switch(this->curKind()) {
    case COMMAND:
        if(this->inTyping()) {
            bool stmt = this->inCmdStmt();
            return createAndCompleter(
                    this->createCmdNameCompleter(CompType::CUR),
                    this->createKeywordCompleter(CompType::CUR, !stmt));
        }
        return this->createCmdArgCompleter(CompType::NONE); // complete command argument
    case CMD_ARG_PART:
        if(this->inTyping()) {
            auto &tokenPairs = this->tracker.getTokenPairs();
            Token token = tokenPairs.back().second;
            assert(tokenPairs.size() > 1);

            unsigned int prevIndex = tokenPairs.size() - 2;
            auto prevKind = tokenPairs[prevIndex].first;
            auto prevToken = tokenPairs[prevIndex].second;

            /**
             * > ./
             * >./
             *
             * complete redirection target
             */
            if(isRedirOp(prevKind)) {
                return this->createFileNameCompleter(CompType::CUR);
            }

            /**
             * if spaces exist between current and previous
             * echo ${false} ./
             *
             * complete command argument
             */
            if(prevToken.pos + prevToken.size < token.pos) {
                return this->createCmdArgCompleter(CompType::CUR);
            }
            return nullptr;
        }
        return this->createCmdArgCompleter(CompType::NONE); // complete command argument
    default:
        if(this->inTyping()) {
            if(isStmtKeyword(this->curKind())) {
                return createAndCompleter(
                        this->createCmdNameCompleter(CompType::CUR),
                        this->createKeywordCompleter(CompType::CUR, false));
            }
        } else if(!exactly && this->afterTyping()) {
            return this->createCmdArgCompleter(CompType::NONE); // complete command argument
        }
        return nullptr;
    }
}

std::unique_ptr<Completer> CompleterFactory::selectCompleter() const {
    if(!this->parser.hasError()) {
        if(this->tracker.getTokenPairs().empty()) {
            return nullptr;
        }

        switch(this->curKind()) {
        case LINE_END:
        case BACKGROUND:
        case DISOWN_BG:
            return createAndCompleter(
                    this->createCmdNameCompleter(CompType::NONE),
                    this->createKeywordCompleter(CompType::NONE, false));
        case RP:
            return std::make_unique<ExpectedTokenCompleter>(";");
        case APPLIED_NAME:
        case SPECIAL_NAME:
            if(this->inTyping()) {
                return this->createGlobalVarNameCompleter(this->curToken());
            }
            break;
        case IDENTIFIER:
            if(this->node->is(NodeKind::VarDecl)
               && static_cast<const VarDeclNode&>(*this->node).getKind() == VarDeclNode::IMPORT_ENV) {
                if(inTyping()) {
                    return this->createEnvNameCompleter(CompType::CUR);
                }
            }
            break;
        default:
            break;
        }
        if(this->inCommand() || this->requireSingleCmdArg()) {
            return this->selectWithCmd();
        }
        if(this->requireMod()) {
            return this->createModNameCompleter(CompType::CUR);
        }
    } else {
        LOG(DUMP_CONSOLE, "error kind: %s\nkind: %s, token: %s, text: %s",
            this->parser.getError().getErrorKind(), toString(this->errorTokenKind()),
            toString(this->errorToken()).c_str(), this->lexer.toTokenText(this->errorToken()).c_str());

        switch(this->errorTokenKind()) {
        case EOS: {
            auto kind = this->curKind();
            if(kind == APPLIED_NAME || kind == SPECIAL_NAME) {
                if(this->inTyping()) {
                    return this->createGlobalVarNameCompleter(this->curToken());
                }
            }

            auto ret = this->selectWithCmd(true);
            if(ret) {
                return ret;
            }

            if(this->inTyping()) {
                return std::make_unique<ExpectedTokenCompleter>("");
            }

            if(this->isErrorKind(NO_VIABLE_ALTER)) {
                std::unique_ptr<Completer> comp;
                if(this->foundExpected(COMMAND)) {
                    comp = this->createCmdNameCompleter(CompType::NONE);
                } else if(this->foundExpected(CMD_ARG_PART)) { // complete redirection target
                    comp = this->createFileNameCompleter(CompType::NONE);
                }
                return createAndCompleter(std::move(comp),
                        this->createExpectedTokenCompleter());
            } else if(this->isErrorKind(TOKEN_MISMATCHED)) {
                LOG(DUMP_CONSOLE, "expected: %s", toString(this->parser.getError().getExpectedTokens().back()));

                if((kind == SOURCE || kind == SOURCE_OPT) && this->foundExpected(CMD_ARG_PART) && this->afterTyping()) {
                    return this->createModNameCompleter(CompType::NONE);
                }

                if(kind == IMPORT_ENV && this->foundExpected(IDENTIFIER) && this->afterTyping()) {
                    return this->createEnvNameCompleter(CompType::NONE);
                }
                return this->createExpectedTokenCompleter();
            }
            break;
        }
        case INVALID: {
            const Token errorToken = this->errorToken();
            if(this->lexer.equals(errorToken, "$") && this->inTyping(errorToken) &&
                this->foundExpected(APPLIED_NAME) && this->foundExpected(SPECIAL_NAME)) {
                return this->createGlobalVarNameCompleter(errorToken);
            }
            break;
        }
        default:
            break;
        }
    }
    return nullptr;
}

void completeLine(DSState &st, const char *data, unsigned int size) {
    CompleterFactory factory(st, data, size);
    auto comp = factory();
    if(comp) {
        auto result = DSValue::create<ArrayObject>(st.symbolTable.get(TYPE::StringArray));
        auto &compreply = *typeAs<ArrayObject>(result);

        (*comp)(compreply);
        auto &values = compreply.refValues();
        compreply.sortAsStrArray();
        auto iter = std::unique(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
            return x.asStrRef() == y.asStrRef();
        });
        values.erase(iter, values.end());

        // override COMPREPLY
        st.setGlobal(toIndex(BuiltinVarOffset::COMPREPLY), std::move(result));
    }
}

} // namespace ydsh