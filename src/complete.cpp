/*
 * Copyright (C) 2020 Nagisa Sekiguchi
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
#include <grp.h>

#include "complete.h"
#include "frontend.h"
#include "vm.h"
#include "logger.h"
#include "misc/files.h"
#include "misc/num_util.hpp"

extern char **environ;  //NOLINT

namespace ydsh {

// for input completion
enum class EscapeOp {
    NOP,
    COMMAND_NAME,
    COMMAND_NAME_PART,
    COMMAND_ARG,
};

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
        buf += ref;
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

static void append(ArrayObject &can, StringRef ref, EscapeOp op) {
    assert(can.getTypeID() == static_cast<unsigned int>(TYPE::StringArray));
    std::string estr = escape(ref, op);
    can.append(DSValue::createStr(std::move(estr)));
}


// ###################################
// ##     CodeCompletionHandler     ##
// ###################################

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

static void completeKeyword(const std::string &prefix, bool onlyExpr, ArrayObject &results) {
    TokenKind table[] = {
#define GEN_ITEM(T) T,
            EACH_LA_statement(GEN_ITEM)
#undef GEN_ITEM
    };
    for(auto &e : table) {
        if(onlyExpr && !isExprKeyword(e)) {
            continue;
        }
        StringRef value = toString(e);
        if(isKeyword(value) && value.startsWith(prefix)) {
            append(results, value, EscapeOp::NOP);
        }
    }
}

static void completeCmdArgKeyword(const std::string &prefix, ArrayObject &results) {
    TokenKind table[] = {
#define GEN_ITEM(T) T,
            EACH_LA_cmdArg(GEN_ITEM)
#undef GEN_ITEM
    };
    for(auto &e : table) {
        StringRef ref = toString(e);
        if(isKeyword(ref) && ref.startsWith(prefix)) {
            append(results, ref, EscapeOp::NOP);
        }
    }
}

static void completeEnvName(const std::string &namePrefix, ArrayObject &results) {
    for(unsigned int i = 0; environ[i] != nullptr; i++) {
        StringRef env(environ[i]);
        auto r = env.indexOf("=");
        assert(r != StringRef::npos);
        auto name = env.substr(0, r);
        if(name.startsWith(namePrefix)) {
            append(results, name, EscapeOp::COMMAND_ARG);
        }
    }
}

static void completeSigName(const std::string &prefix, ArrayObject &results) {
    auto *list = getSignalList();
    for(unsigned int i = 0; list[i].name != nullptr; i++) {
        StringRef sigName = list[i].name;
        if(sigName.startsWith(prefix)) {
            append(results, sigName, EscapeOp::NOP);
        }
    }
}

static void completeUserName(const std::string &prefix, ArrayObject &results) {
    setpwent();
    for(struct passwd *pw; (pw = getpwent()) != nullptr;) {
        StringRef pname = pw->pw_name;
        if(pname.startsWith(prefix)) {
            append(results, pname, EscapeOp::NOP);
        }
    }
    endpwent();
}

static void completeGroupName(const std::string &prefix, ArrayObject &results) {
    setgrent();
    for(struct group *gp; (gp = getgrent()) != nullptr;) {
        StringRef gname = gp->gr_name;
        if(gname.startsWith(prefix)) {
            append(results, gname, EscapeOp::NOP);
        }
    }
    endgrent();
}

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

static void completeCmdName(const SymbolTable &symbolTable, const std::string &cmdPrefix,
                            const CodeCompOp option, ArrayObject &results) {
    // complete user-defined command
    if(hasFlag(option, CodeCompOp::UDC)) {
        for(const auto &iter : symbolTable.globalScope()) {
            StringRef udc = iter.first.c_str();
            if(udc.startsWith(CMD_SYMBOL_PREFIX)) {
                udc.removePrefix(strlen(CMD_SYMBOL_PREFIX));
                if(udc.startsWith(cmdPrefix)) {
                    if(std::any_of(std::begin(DENIED_REDEFINED_CMD_LIST),
                                   std::end(DENIED_REDEFINED_CMD_LIST),
                                   [&](auto &e){ return udc == e; })) {
                        continue;
                    }
                    append(results, udc, EscapeOp::COMMAND_NAME);
                }
            }
        }
    }

    // complete builtin command
    if(hasFlag(option, CodeCompOp::BUILTIN)) {
        unsigned int bsize = getBuiltinCommandSize();
        for(unsigned int i = 0; i < bsize; i++) {
            StringRef builtin = getBuiltinCommandName(i);
            if(builtin.startsWith(cmdPrefix)) {
                append(results, builtin, EscapeOp::COMMAND_NAME);
            }
        }
    }

    // complete external command
    if(hasFlag(option, CodeCompOp::EXTERNAL)) {
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
                StringRef cmd = entry->d_name;
                if(cmd.startsWith(cmdPrefix)) {
                    std::string fullpath(p);
                    fullpath += '/';
                    fullpath += cmd.data();
                    if(isExecutable(fullpath.c_str())) {
                        append(results, cmd, EscapeOp::COMMAND_NAME);
                    }
                }
            }
            closedir(dir);
        }
    }
}

static void completeFileName(const char *baseDir, const std::string &prefix,
                            const CodeCompOp op, ArrayObject &results) {
    const auto s = prefix.find_last_of('/');

    // complete tilde
    if(prefix[0] == '~' && s == std::string::npos && hasFlag(op, CodeCompOp::TILDE)) {
        setpwent();
        for(struct passwd *entry; (entry = getpwent()) != nullptr;) {
            StringRef pwname = entry->pw_name;
            if(pwname.startsWith(prefix.c_str() + 1)) {
                std::string name("~");
                name += entry->pw_name;
                name += '/';
                append(results, name.c_str(), EscapeOp::NOP);
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
        targetDir = prefix.substr(0, s);
        if(hasFlag(op, CodeCompOp::TILDE)) {
            expandTilde(targetDir, true);
        }
        targetDir = expandDots(baseDir, targetDir.c_str());
    } else {
        targetDir = expandDots(baseDir, ".");
    }
    LOG(DUMP_CONSOLE, "targetDir = %s", targetDir.c_str());

    /**
     * resolve name
     */
    std::string name;
    if(s != std::string::npos) {
        name = prefix.substr(s + 1);
    } else {
        name = prefix;
    }

    DIR *dir = opendir(targetDir.c_str());
    if(dir == nullptr) {
        return;
    }

    for(dirent *entry; (entry = readdir(dir)) != nullptr;) {
        StringRef dname = entry->d_name;
        if(dname.startsWith(name.c_str())) {
            if(name.empty() && (dname == ".." || dname == ".")) {
                continue;
            }

            std::string fullpath(targetDir);
            fullpath += '/';
            fullpath += entry->d_name;

            if(S_ISDIR(getStMode(fullpath.c_str()))) {
                fullpath += '/';
            } else {
                if(hasFlag(op, CodeCompOp::EXEC)) {
                    if(S_ISREG(getStMode(fullpath.c_str())) && access(fullpath.c_str(), X_OK) != 0) {
                        continue;
                    }
                } else if(hasFlag(op, CodeCompOp::DIR) && !hasFlag(op, CodeCompOp::FILE)) {
                    continue;
                }
            }

            StringRef fileName = fullpath;
            unsigned int len = strlen(entry->d_name);
            if(fileName.back() == '/') {
                len++;
            }
            fileName.removePrefix(fileName.size() - len);
            append(results, fileName,
                   hasFlag(op, CodeCompOp::EXEC) ? EscapeOp::COMMAND_NAME_PART : EscapeOp::COMMAND_ARG);
        }
    }
    closedir(dir);
}

static void completeModule(const char *scriptDir, const std::string &prefix, bool tilde, ArrayObject &results) {
    CodeCompOp op{};
    if(tilde) {
        op = CodeCompOp::TILDE;
    }

    // complete from SCRIPT_DIR
    completeFileName(scriptDir, prefix, op, results);

    // complete from local module dir
    std::string localModDir = LOCAL_MOD_DIR;
    expandTilde(localModDir);
    completeFileName(localModDir.c_str(), prefix, op, results);

    // complete from system module dir
    completeFileName(SYSTEM_MOD_DIR, prefix, op, results);
}

/**
 *
 * @param symbolTable
 * @param prefix
 * not start with '$'
 * @param results
 */
static void completeVarName(const SymbolTable &symbolTable,
                            const std::string &prefix, ArrayObject &results) {
    for(const auto &iter : symbolTable.globalScope()) {
        StringRef varName = iter.first.c_str();
        if(!varName.startsWith(CMD_SYMBOL_PREFIX)
            && varName.startsWith(prefix)) {
            append(results, varName, EscapeOp::NOP);
        }
    }
}

static void completeExpected(const std::vector<std::string> &expected, ArrayObject &results) {
    for(auto &e : expected) {
        if(isKeyword(e)) {
            append(results, e, EscapeOp::NOP);
        }
    }
}

DSValue createArgv(const DSState &state, const Lexer &lex,
                   const CmdNode &cmdNode, const std::string &word) {
    std::vector<DSValue> values;

    // add cmd
    values.push_back(DSValue::createStr(lex.toStrRef(cmdNode.getNameNode().getToken())));

    // add args
    for(auto &e : cmdNode.getArgNodes()) {
        if(isa<RedirNode>(*e)) {
            continue;
        }
        values.push_back(DSValue::createStr(lex.toStrRef(e->getToken())));
    }

    // add last arg
    if(!word.empty()) {
        values.push_back(DSValue::createStr(word));
    }

    return DSValue::create<ArrayObject>(state.typePool.get(TYPE::StringArray), std::move(values));
}

static bool kickCompHook(DSState &state, const Lexer &lex, const CmdNode &cmdNode,
                         const std::string &word, ArrayObject &results) {
    auto hook = getGlobal(state, VAR_COMP_HOOK);
    if(hook.isInvalid()) {
        return false;
    }

    // preapre argument
    auto argv = createArgv(state, lex, cmdNode, word);
    unsigned int index = typeAs<ArrayObject>(argv).size();
    if(!word.empty()) {
        index--;
    }

    // kick hook
    auto ret = callFunction(state, std::move(hook),
                            makeArgs(std::move(argv), DSValue::createInt(index)));
    if(state.hasError() || typeAs<ArrayObject>(ret).size() == 0) {
        return false;
    }

    for(auto &e : typeAs<ArrayObject>(ret).getValues()) {
        append(results, e.asCStr(), EscapeOp::COMMAND_ARG);
    }
    return true;
}

void CodeCompletionHandler::invoke(ArrayObject &results) {
    if(!this->hasCompRequest()) {
        return; // do nothing
    }

    if(hasFlag(this->compOp, CodeCompOp::ENV)) {
        completeEnvName(this->compWord, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::SIGNAL)) {
        completeSigName(this->compWord, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::EXTERNAL) ||
       hasFlag(this->compOp, CodeCompOp::UDC) || hasFlag(this->compOp, CodeCompOp::BUILTIN)) {
        completeCmdName(this->state.symbolTable, this->compWord, this->compOp, results);    //FIXME: file name or tilde expansion
    }
    if(hasFlag(this->compOp, CodeCompOp::USER)) {
        completeUserName(this->compWord, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::GROUP)) {
        completeGroupName(this->compWord, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::FILE) || hasFlag(this->compOp, CodeCompOp::EXEC) ||
        hasFlag(this->compOp, CodeCompOp::DIR)) {
        completeFileName(this->state.logicalWorkingDir.c_str(), this->compWord, this->compOp, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::MODULE)) {
        completeModule(this->scriptDir.empty() ? getCWD().get() : this->scriptDir.c_str(),
                       this->compWord, hasFlag(this->compOp, CodeCompOp::TILDE), results);
    }
    if(hasFlag(this->compOp, CodeCompOp::STMT_KW) || hasFlag(this->compOp, CodeCompOp::EXPR_KW)) {
        bool onlyExpr = !hasFlag(this->compOp, CodeCompOp::STMT_KW);
        completeKeyword(this->compWord, onlyExpr, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::ARG_KW)) {
        completeCmdArgKeyword(this->compWord, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::GVAR)) {
        completeVarName(this->state.symbolTable, this->compWord, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::EXPECT)) {
        completeExpected(this->extraWords, results);
    }
    if(hasFlag(this->compOp, CodeCompOp::HOOK)) {
        if(!kickCompHook(this->state, *this->lex, *this->cmdNode, this->compWord, results)) {
            completeFileName(state.logicalWorkingDir.c_str(), this->compWord, this->fallbackOp, results);
        }
    }
}

void CodeCompletionHandler::addVarNameRequest(Token token) {
    auto value = this->lex->toName(token);
    this->addCompRequest(CodeCompOp::GVAR, std::move(value));
}

void CodeCompletionHandler::addCmdRequest(const Token token) {
    auto value = this->lex->toCmdArg(token);
    bool tilde = this->lex->startsWith(token, '~');
    bool isDir = strchr(value.c_str(), '/') != nullptr;
    if(tilde || isDir) {
        CodeCompOp op = CodeCompOp::EXEC;
        if(tilde) {
            setFlag(op, CodeCompOp::TILDE);
        }
        this->addCompRequest(op, std::move(value));
    } else {
        this->addCompRequest(CodeCompOp::COMMAND, std::move(value));
    }
}

void CodeCompletionHandler::addCmdArgOrModRequest(Token token, CmdArgParseOpt opt) {
    CodeCompOp op{};
    if(hasFlag(opt, CmdArgParseOpt::FIRST)) {
        if(this->lex->startsWith(token, '~')) {
            setFlag(op, CodeCompOp::TILDE);
        }
        if(hasFlag(opt, CmdArgParseOpt::MODULE)) {
            setFlag(op, CodeCompOp::MODULE);
        } else if(hasFlag(opt, CmdArgParseOpt::REDIR)) {
            setFlag(op, CodeCompOp::FILE | CodeCompOp::ARG_KW);
        } else {
            setFlag(op, CodeCompOp::FILE);
        }
    } else if(hasFlag(opt, CmdArgParseOpt::REDIR)) {
        setFlag(op, CodeCompOp::ARG_KW);
    }
    this->addCompRequest(op, this->lex->toCmdArg(token));
}

static Lexer lex(StringRef ref) {
    return Lexer("<line>", ByteBuffer(ref.begin(), ref.end()), getCWD());
}

static void consumeAllInput(FrontEnd &frontEnd) {
    while(frontEnd) {
        if(!frontEnd(nullptr)) {
            break;
        }
    }
}

unsigned int doCodeCompletion(DSState &st, StringRef ref, CodeCompOp option) {
    auto result = DSValue::create<ArrayObject>(st.typePool.get(TYPE::StringArray));
    auto &compreply = typeAs<ArrayObject>(result);

    DiscardPoint discardPoint {
        .symbol = st.symbolTable.getDiscardPoint(),
        .type = st.typePool.getDiscardPoint(),
    };
    CodeCompletionHandler handler(st);
    if(empty(option)) { // invoke parser
        // prepare
        FrontEnd frontEnd(lex(ref), st.typePool, st.symbolTable, DS_EXEC_MODE_PARSE_ONLY, false,
                          ObserverPtr<CodeCompletionHandler>(&handler));    //FIXME: sematic aware completion

        // perform completion
        consumeAllInput(frontEnd);
        handler.invoke(compreply);
    } else {
        handler.addCompRequest(option, ref.toString());
        handler.invoke(compreply);
    }
    discardAll(st.symbolTable, st.typePool, discardPoint);

    auto &values = compreply.refValues();
    compreply.sortAsStrArray();
    auto iter = std::unique(values.begin(), values.end(), [](const DSValue &x, const DSValue &y) {
        return x.asStrRef() == y.asStrRef();
    });
    values.erase(iter, values.end());

    // override COMPREPLY
    st.setGlobal(toIndex(BuiltinVarOffset::COMPREPLY), std::move(result));
    return values.size();
}


} // namespace ydsh