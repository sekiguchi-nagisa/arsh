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

// copied from complete.cpp
static bool needEscape(char ch, EscapeOp op) {
    switch(ch) {
    case ' ': case ';':
    case '\'': case '"': case '`':
    case '|': case '&': case '<':
    case '>': case '(': case ')':
    case '$': case '#': case '~':
        return true;
    case '{': case '}':
        if(op == EscapeOp::COMMAND_NAME || op == EscapeOp::COMMAND_NAME_PART) {
            return true;
        }
        break;
    default:
        if((ch >= 0 && ch < 32) || ch == 127) {
            return true;
        }
        break;
    }
    return false;
}

static std::string escape(StringRef ref, EscapeOp op) {
    std::string buf;
    if(op == EscapeOp::NOP) {
        buf.append(ref.data(), ref.size());
        return buf;
    }

    auto iter = ref.begin();
    const auto end = ref.end();

    if(op == EscapeOp::COMMAND_NAME) {
        char ch = *iter;
        if(isDecimal(ch) || ch == '+' || ch == '-' || ch == '[' || ch == ']') {
            buf += '\\';
            buf += static_cast<char>(ch);
            iter++;
        }
    }

    while(iter != end) {
        char ch = *(iter++);
        if(ch == '\\' && iter != end && needEscape(*iter, op)) {
            buf += '\\';
            ch = *(iter++);
        } else if(needEscape(ch, op)) {
            if((ch >= 0 && ch < 32) || ch == 127) {
                char d[32];
                snprintf(d, arraySize(d), "$'\\x%02x'", ch);
                buf += d;
                continue;
            } else {
                buf += '\\';
            }
        }
        buf += ch;
    }
    return buf;
}

static std::string escape2(const char *str, EscapeOp op) {
    return escape(str, op);
}

static void append(ArrayObject &can, const char *str, EscapeOp op) {
    assert(can.getTypeID() == static_cast<unsigned int>(TYPE::StringArray));
    std::string estr = escape2(str, op);
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
            expandTilde(targetDir, true);
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

        for(auto &e : typeAs<ArrayObject>(result).getValues()) {
            append(ret, e.asCStr(), EscapeOp::COMMAND_ARG);
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
        return inCmdMode(*cast<const UnaryOpNode>(node).getExprNode());
    case NodeKind::BinaryOp:
        return inCmdMode(*cast<const BinaryOpNode>(node).getRightNode());
    case NodeKind::Cmd:
        return cast<const CmdNode>(&node);
    case NodeKind::Pipeline:
        return inCmdMode(*cast<const PipelineNode>(node).getNodes().back());
    case NodeKind::Fork: {
        auto &forkNode = cast<const ForkNode>(node);
        return forkNode.getOpKind() == ForkKind::COPROC ? inCmdMode(forkNode.getExprNode()) : nullptr;
    }
    case NodeKind::Assert: {
        auto &assertNode = cast<const AssertNode>(node);
        return assertNode.getMessageNode().getToken().size == 0 ? inCmdMode(assertNode.getCondNode()) : nullptr;
    }
    case NodeKind::Jump: {
        auto &jumpNode = cast<const JumpNode>(node);
        return jumpNode.getOpKind() != JumpNode::CONTINUE_ ? inCmdMode(jumpNode.getExprNode()) : nullptr;
    }
    case NodeKind::VarDecl: {
        auto &vardeclNode = cast<const VarDeclNode>(node);
        return vardeclNode.getExprNode() != nullptr ? inCmdMode(*vardeclNode.getExprNode()) : nullptr;
    }
    case NodeKind::Assign:
        return inCmdMode(cast<const AssignNode>(node).getRightNode());
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
    CompleterFactory(DSState &st, StringRef ref);

private:
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
        return this->node && this->inCommand() && isa<CmdNode>(*this->node);
    }

    bool requireSingleCmdArg() const {
        if(this->inTyping()) {
            if(isa<WithNode>(*this->node)) {
                return true;
            }
        }
        return false;
    }

    bool requireMod() const {
        if(this->inTyping()) {
            if(isa<SourceListNode>(*this->node)) {
                return cast<const SourceListNode>(*this->node).getName().empty();
            }
        }
        return false;
    }

    DSValue newStrObj(Token token) const {
        return DSValue::createStr(this->lexer.toStrRef(token));
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

CompleterFactory::CompleterFactory(DSState &st, StringRef ref) :
        state(st), cursor(ref.size()),
        lexer("<line>", ByteBuffer(ref.begin(), ref.end()), getCWD()), parser(this->lexer) {
    LOG(DUMP_CONSOLE, "line: %s, cursor: %zu", ref.toString().c_str(), ref.size());
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
        if(isa<RedirNode>(*argNode)) {
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
            return nullptr;
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
//                return this->createFileNameCompleter(CompType::CUR);
                return nullptr;
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
//            if(isStmtKeyword(this->curKind())) {
//                return createAndCompleter(
//                        this->createCmdNameCompleter(CompType::CUR),
//                        this->createKeywordCompleter(CompType::CUR, false));
//            }
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
        if(this->inCommand() || this->requireSingleCmdArg()) {
            return this->selectWithCmd();
        }
    }
    return nullptr;
}

void oldCompleteLine(DSState &st, StringRef ref, ArrayObject &compreply) {
    CompleterFactory factory(st, ref);
    auto comp = factory();
    if(comp) {
        (*comp)(compreply);
    }
}

} // namespace ydsh