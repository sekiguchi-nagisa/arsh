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
#include <sys/types.h>
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
        char ch = *str;
        if(isDecimal(ch) || ch == '+' || ch == '-' || ch == '[' || ch == ']') {
            buf += '\\';
            buf += ch;
            str++;
        }
    }

    while(*str != '\0') {
        char ch = *(str++);
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
        buf += ch;
    }

    buf += '\0';
    return buf;
}


static void append(Array_Object &can, const char *str, EscapeOp op) {
    assert(can.getType()->is(TYPE::StringArray));
    auto *type = static_cast<ReifiedType *>(can.getType())->getElementTypes()[0];
    assert(type->is(TYPE::String));
    std::string estr = escape(str, op);
    can.append(DSValue::create<String_Object>(*type, std::move(estr)));
}

static void append(Array_Object &buf, const std::string &str, EscapeOp op) {
    append(buf, str.c_str(), op);
}

static bool startsWith(const char *s1, const char *s2) {
    return s1 != nullptr && s2 != nullptr && strstr(s1, s2) == s1;
}

struct Completer {
    virtual void operator()(Array_Object &ret) = 0;

    /**
     * for debugging.
     * @return
     */
    virtual const char *name() const = 0;

    virtual ~Completer() = default;
};

class ExpectedTokenCompleter : public Completer {
private:
    const std::string token;

public:
    explicit ExpectedTokenCompleter(std::string &&token) : token(std::move(token)) {}

    void operator()(Array_Object &results) override {
        if(!this->token.empty()) {
            append(results, this->token, EscapeOp::NOP);
        }
    }

    const char *name() const override {
        return "Expected";
    }
};

class EnvNameCompleter : public Completer {
private:
    const std::string envName;

public:
    explicit EnvNameCompleter(std::string &&name) : envName(std::move(name)) {}

    void operator()(Array_Object &results) override {
        for(unsigned int i = 0; environ[i] != nullptr; i++) {
            const char *env = environ[i];
            if(startsWith(env, this->envName.c_str())) {
                const char *ptr = strchr(env, '=');
                assert(ptr != nullptr);
                append(results, std::string(env, ptr - env), EscapeOp::NOP);
            }
        }
    }

    const char *name() const override {
        return "Env";
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
            symbolTable(symbolTable), token(std::move(token)) {}

    void operator()(Array_Object &results) override;

    const char *name() const override {
        return "Command";
    }
};

static std::vector<std::string> computePathList(const char *pathVal) {
    std::vector<std::string> result;
    result.emplace_back();
    assert(pathVal != nullptr);

    for(unsigned int i = 0; pathVal[i] != '\0'; i++) {
        char ch = pathVal[i];
        if(ch == ':') {
            result.emplace_back();
        } else {
            result.back() += ch;
        }
    }

    // expand tilde
    for(auto &s : result) {
        expandTilde(s);
    }

    return result;
}

void CmdNameCompleter::operator()(Array_Object &results) {
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
            symbolTable(symbolTable), token(std::move(token)) {}

    void operator()(Array_Object &results) override {
        for(const auto &iter : this->symbolTable.globalScope()) {
            const char *varName = iter.first.c_str();
            if(!this->token.empty() && !startsWith(varName, CMD_SYMBOL_PREFIX)
               && !startsWith(varName, MOD_SYMBOL_PREFIX)
               && startsWith(varName, this->token.c_str() + 1)) {
                append(results, iter.first, EscapeOp::NOP);
            }
        }
    }

    const char *name() const override {
        return "GlobalVar";
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
            baseDir(baseDir), token(std::move(token)), op(op) {}

protected:
    bool onlyExec() const {
        return hasFlag(this->op, FileNameCompOp::ONLY_EXEC);
    }

    bool tilde() const {
        return hasFlag(this->op, FileNameCompOp::TIDLE);
    }

public:
    void operator()(Array_Object &results) override;

    const char *name() const override {
        return this->onlyExec() ? "QualifiedCommand" : "FileName";
    }
};

void FileNameCompleter::operator()(Array_Object &results) {
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
            FileNameCompleter(scriptDir, std::move(token), tilde ? FileNameCompOp::TIDLE : FileNameCompOp{}) {}

    void operator()(Array_Object &results) override {
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

    const char *name() const override {
        return "Module";
    }
};

static bool isRedirOp(TokenKind kind) {
    switch(kind) {
    case REDIR_IN_2_FILE:
    case REDIR_OUT_2_FILE:
    case REDIR_OUT_2_FILE_APPEND:
    case REDIR_ERR_2_FILE:
    case REDIR_ERR_2_FILE_APPEND:
    case REDIR_MERGE_ERR_2_OUT_2_FILE:
    case REDIR_MERGE_ERR_2_OUT_2_FILE_APPEND:
//    case REDIR_MERGE_ERR_2_OUT:
//    case REDIR_MERGE_OUT_2_ERR:   // has no target
    case REDIR_HERE_STR:
        return true;
    default:
        return false;
    }
}

static bool findKind(const std::vector<TokenKind> &values, TokenKind kind) {
    for(auto &e : values) {
        if(e == kind) {
            return true;
        }
    }
    return false;
}

static bool inCmdMode(const Node &node) {
    switch(node.getNodeKind()) {
    case NodeKind::UnaryOp:
        return inCmdMode(*static_cast<const UnaryOpNode &>(node).getExprNode());
    case NodeKind::BinaryOp:
        return inCmdMode(*static_cast<const BinaryOpNode &>(node).getRightNode());
    case NodeKind::Cmd:
        return true;
    case NodeKind::Pipeline:
        return inCmdMode(*static_cast<const PipelineNode &>(node).getNodes().back());
    case NodeKind::Fork: {
        auto &forkNode = static_cast<const ForkNode &>(node);
        return forkNode.getOpKind() == ForkKind::COPROC && inCmdMode(*forkNode.getExprNode());
    }
    case NodeKind::Assert: {
        auto &assertNode = static_cast<const AssertNode &>(node);
        return assertNode.getMessageNode()->getToken().size == 0 && inCmdMode(*assertNode.getCondNode());
    }
    case NodeKind::Jump: {
        auto &jumpNode = static_cast<const JumpNode &>(node);
        return (jumpNode.getOpKind() == JumpNode::THROW || jumpNode.getOpKind() == JumpNode::RETURN)
               && inCmdMode(*jumpNode.getExprNode());
    }
    case NodeKind::VarDecl: {
        auto &vardeclNode = static_cast<const VarDeclNode &>(node);
        return vardeclNode.getExprNode() != nullptr && inCmdMode(*vardeclNode.getExprNode());
    }
    case NodeKind::Assign:
        return inCmdMode(*static_cast<const AssignNode &>(node).getRightNode());
    default:
        break;
    }
    return false;
}

static bool requireSingleCmdArg(const Node &node, unsigned int cursor) {
    auto token = node.getToken();
    if(token.pos + token.size == cursor) {
        auto kind = node.getNodeKind();
        if(kind == NodeKind::With) {
            return true;
        }
    }
    return false;
}

static bool requireMod(const Node &node, unsigned int cursor) {
    auto token = node.getToken();
    if(token.pos + token.size == cursor) {
        auto kind = node.getNodeKind();
        if(kind == NodeKind::Source) {
            return static_cast<const SourceNode&>(node).getName().empty();
        }
    }
    return false;
}

class CompleterFactory { //TODO: type aware completion
private:
    DSState &state;

    const unsigned int cursor;

    Lexer lexer;

    Parser parser;

    TokenTracker tracker;

public:
    CompleterFactory(DSState &st, const std::string &line);

private:
    std::unique_ptr<Completer> createModNameCompleter() const {
        return std::make_unique<ModNameCompleter>(this->state.getScriptDir(), "", false);
    }

    std::unique_ptr<Completer> createModNameCompleter(Token token) const {
        bool tilde = this->lexer.startsWith(token, '~');
        return std::make_unique<ModNameCompleter>(this->state.getScriptDir(), this->lexer.toCmdArg(token), tilde);
    }

    std::unique_ptr<Completer> createFileNameCompleter() const {
        return std::make_unique<FileNameCompleter>(this->state.logicalWorkingDir.c_str(), "");
    }

    std::unique_ptr<Completer> createFileNameCompleter(Token token) const {
        FileNameCompOp op{};
        if(this->lexer.startsWith(token, '~')) {
            setFlag(op, FileNameCompOp::TIDLE);
        }
        return std::make_unique<FileNameCompleter>(this->state.logicalWorkingDir.c_str(), this->lexer.toCmdArg(token), op);
    }

    std::unique_ptr<Completer> createCmdNameCompleter() const {
        return std::make_unique<CmdNameCompleter>(this->state.symbolTable, "");
    }

    std::unique_ptr<Completer> createCmdNameCompleter(Token token) const {
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

    std::unique_ptr<Node> applyAndGetLatest() {
        std::unique_ptr<Node> node;
        while(this->parser) {
            node = this->parser();
            if(this->parser.hasError()) {
                break;
            }
        }
        return node;
    }

    std::unique_ptr<Completer> selectWithCmd(bool exactly = false);

    std::unique_ptr<Completer> selectCompleter();

public:
    std::unique_ptr<Completer> operator()() {
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
            str += (ret ? ret->name() : "None");
            return str;
        });

        return ret;
    }
};

// ##############################
// ##     CompleterFactory     ##
// ##############################

CompleterFactory::CompleterFactory(DSState &st, const std::string &line) :
        state(st), cursor(line.size() - 1),
        lexer("<line>", line.c_str()), parser(this->lexer) {
    this->parser.setTracker(&this->tracker);
}

std::unique_ptr<Completer> CompleterFactory::selectWithCmd(bool exactly) {
    auto &tokenPairs = this->tracker.getTokenPairs();
    assert(!tokenPairs.empty());
    TokenKind kind = tokenPairs.back().first;
    Token token = tokenPairs.back().second;

    switch(kind) {
    case COMMAND:
        if(token.pos + token.size == cursor) {
            return this->createCmdNameCompleter(token);
        }
        return this->createFileNameCompleter();
    case CMD_ARG_PART:
        if(token.pos + token.size == cursor && tokenPairs.size() > 1) {
            unsigned int prevIndex = tokenPairs.size() - 2;
            auto prevKind = tokenPairs[prevIndex].first;
            auto prevToken = tokenPairs[prevIndex].second;

            /**
             * if previous token is redir op,
             * or if spaces exist between current and previous
             */
            if(isRedirOp(prevKind) || prevToken.pos + prevToken.size < token.pos) {
                return this->createFileNameCompleter(token);
            }
            return nullptr;
        }
        return this->createFileNameCompleter();
    default:
        if(!exactly && token.pos + token.size < cursor) {
            return this->createFileNameCompleter();
        }
        return nullptr;
    }
}

std::unique_ptr<Completer> CompleterFactory::selectCompleter() {
    auto node = this->applyAndGetLatest();
    const auto &tokenPairs = this->tracker.getTokenPairs();

    if(!this->parser.hasError()) {
        if(tokenPairs.empty()) {
            return nullptr;
        }

        auto token = tokenPairs.back().second;
        switch(tokenPairs.back().first) {
        case LINE_END:
        case BACKGROUND:
        case DISOWN_BG:
            return this->createCmdNameCompleter();
        case RP:
            return std::make_unique<ExpectedTokenCompleter>(";");
        case APPLIED_NAME:
        case SPECIAL_NAME:
            if(token.pos + token.size == cursor) {
                return this->createGlobalVarNameCompleter(token);
            }
            break;
        case IDENTIFIER:
            if(node->getNodeKind() == NodeKind::VarDecl
               && static_cast<const VarDeclNode&>(*node).getKind() == VarDeclNode::IMPORT_ENV) {
                if(token.pos + token.size == cursor) {
                    return std::make_unique<EnvNameCompleter>(this->lexer.toTokenText(token));
                }
            }
            break;
        default:
            break;
        }
        if(inCmdMode(*node) || requireSingleCmdArg(*node, this->cursor)) {
            return this->selectWithCmd();
        }
        if(requireMod(*node, this->cursor)) {
            return this->createModNameCompleter(token);
        }
    } else {
        const auto &e = this->parser.getError();
        LOG(DUMP_CONSOLE, "error kind: %s\nkind: %s, token: %s, text: %s",
            e.getErrorKind(), toString(e.getTokenKind()),
            toString(e.getErrorToken()).c_str(),
            this->lexer.toTokenText(e.getErrorToken()).c_str());

        Token token = e.getErrorToken();
        if(token.pos + token.size < this->cursor) {
            return nullptr;
        }

        switch(e.getTokenKind()) {
        case EOS: {
            if(!tokenPairs.empty()) {
                auto kind = tokenPairs.back().first;
                auto token = tokenPairs.back().second;
                if(kind == APPLIED_NAME || kind == SPECIAL_NAME) {
                    if(token.pos + token.size == this->cursor) {
                        return this->createGlobalVarNameCompleter(token);
                    }
                }
            }

            if(strcmp(e.getErrorKind(), NO_VIABLE_ALTER) == 0) {
                if(findKind(e.getExpectedTokens(), COMMAND)) {
                    return this->createCmdNameCompleter();
                }

                if(findKind(e.getExpectedTokens(), CMD_ARG_PART)) {
                    return this->createFileNameCompleter();
                }
            } else if(strcmp(e.getErrorKind(), TOKEN_MISMATCHED) == 0) {
                assert(!e.getExpectedTokens().empty());
                TokenKind expected = e.getExpectedTokens().back();
                LOG(DUMP_CONSOLE, "expected: %s", toString(expected));

                auto ret = this->selectWithCmd(true);
                if(ret) {
                    return ret;
                }

                if(findKind(e.getExpectedTokens(), CMD_ARG_PART)) {
                    if(!tokenPairs.empty() && tokenPairs.back().first == SOURCE) {
                        return this->createModNameCompleter();
                    }
                    return this->createFileNameCompleter(); // complete command argument
                }

                if(!tokenPairs.empty() && tokenPairs.back().first == IMPORT_ENV
                   && findKind(e.getExpectedTokens(), IDENTIFIER)) {
                    return std::make_unique<EnvNameCompleter>("");
                }

                std::string expectedStr = toString(expected);
                if(expectedStr.size() < 2 || (expectedStr.front() != '<' && expectedStr.back() != '>')) {
                    return std::make_unique<ExpectedTokenCompleter>(std::move(expectedStr));
                }
            }
            break;
        }
        case INVALID: {
            if(this->lexer.equals(token, "$") && token.pos + token.size == cursor &&
               findKind(e.getExpectedTokens(), APPLIED_NAME) &&
               findKind(e.getExpectedTokens(), SPECIAL_NAME)) {
                return this->createGlobalVarNameCompleter(token);
            }
            break;
        }
        default:
            break;
        }
    }
    return nullptr;
}

void completeLine(DSState &st, const std::string &line) {
    assert(!line.empty() && line.back() == '\n');

    auto &compreply = *typeAs<Array_Object>(st.getGlobal(BuiltinVarOffset::COMPREPLY));
    compreply.refValues().clear();  // clear old values
    compreply.refValues().shrink_to_fit();

    CompleterFactory factory(st, line);
    auto comp = factory();
    if(comp) {
        (*comp)(compreply);
        auto &values = compreply.refValues();
        std::sort(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
            return typeAs<String_Object>(x)->compare(y);
        });
        auto iter = std::unique(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
            return typeAs<String_Object>(x)->equals(y);
        });
        values.erase(iter, values.end());
    }
}

} // namespace ydsh