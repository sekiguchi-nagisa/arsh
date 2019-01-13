/*
 * Copyright (C) 2018-2019 Nagisa Sekiguchi
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

#ifndef YDSH_TOOLS_LSP_H
#define YDSH_TOOLS_LSP_H

#include "../json/json.h"
#include "../uri/uri.h"

namespace ydsh {
namespace lsp {

using namespace json;

// definition of basic interface of language server protocol
// LSP specific error code
enum LSPErrorCode : int {
    ServerErrorStart     = -32099,
    ServerErrorEnd       = -32000,
    ServerNotInitialized = -32002,
    UnknownErrorCode     = -32001,
    RequestCancelled     = -32800,
    ContentModified      = -32801,
};

struct DocumentURI {
    std::string uri;    // must be valid URI
};

struct Position {
    int line{0};
    int character{0};
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    DocumentURI uri;
    Range range;
};

struct LocationLink {
    Union<Range> originSelectionRange;  // optional
    std::string targetUri;
    Range targetRange;
    Union<Range> targetSelectionRange;  // optional
};


enum class DiagnosticSeverity : int {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
};

struct DiagnosticRelatedInformation {
    Location location;
    std::string message;
};

struct Diagnostic {
    Range range;
    Union<DiagnosticSeverity> severity; // optional
//    std::string code; // string | number, //FIXME: currently not supported.
//    std::string source;                   //FIXME: currently not supported.
    std::string message;
    Union<std::vector<DiagnosticRelatedInformation>> relatedInformation;   // optional
};

struct Command {
    std::string title;
    std::string command;
//    std::vector<JSON> arguments // any[], optional  //FIXME: currently not supported.
};

struct TextEdit {
    Range range;
    std::string newText;
};

// for Initialize request

struct ClientCapabilities {
    JSON workspace;   // optional
    JSON textDocument; // optional
};

enum class TraceSetting {
    off,
    messages,
    verbose
};

struct InitializeParams {
    Union<int, std::nullptr_t> processId;
    Union<std::string, std::nullptr_t> rootPath;    // optional
    Union<DocumentURI, std::nullptr_t> rootUri;
    JSON initializationOptions; // optional
    ClientCapabilities capabilities;
    Union<TraceSetting> trace;  // optional
//    Union<WorkspaceFolder, std::nullptr_t> workspaceFolders;    // optional   //FIXME: currently not supported
};

// for server capability
enum class TextDocumentSyncKind : int {
    None = 0,
    Full = 1,
    Incremental = 2,
};

struct CompletionOptions {
    Union<bool> resolveProvider;    // optional
    Union<std::vector<std::string>> triggerCharacters;  // optional
};

struct SignatureHelpOptions {
    Union<std::vector<std::string>> triggerCharacters;  // optional
};

#define EACH_CODE_ACTION_KIND(OP) \
    OP(QuickFix, "quickfix") \
    OP(Refactor, "refactor") \
    OP(RefactorExtract, "refactor.extract") \
    OP(RefactorInline, "refactor.inline") \
    OP(RefactorRewrite, "refactor.rewrite") \
    OP(Source, "source") \
    OP(SourceOrganizeImports, "source.organizeImports")

enum class CodeActionKind : unsigned int {
#define GEN_ENUM(e, s) e,
    EACH_CODE_ACTION_KIND(GEN_ENUM)
#undef GEN_ENUM
};

struct CodeActionOptions {
    Union<std::vector<CodeActionKind>> codeActionKinds; // optional
};

struct CodeLensOptions {
    Union<bool> resolveProvider;    // optional
};

struct DocumentOnTypeFormattingOptions {
    std::string firstTriggerCharacter;
    Union<std::vector<std::string>> moreTriggerCharacter;   // optional
};

struct RenameOptions {
    Union<bool> prepareProvider;    // optional
};

struct DocumentLinkOptions {
    Union<bool> resolveProvider;    // optional
};

struct ExecuteCommandOptions {
    std::vector<std::string> commands;
};

struct SaveOptions {
    Union<bool> includeText;    // optional
};

struct ColorProviderOptions {};
struct FoldingRangeProviderOptions {};

struct TextDocumentSyncOptions {
    Union<bool> openClose;  // optional
    Union<TextDocumentSyncKind> change; // optional
    Union<bool> willSave;   // optional
    Union<bool> willSaveWaitUntil;  // optional
    Union<SaveOptions> save;    // optional
};

struct StaticRegistrationOptions {
    Union<std::string> id;  // optional
};

/**
 * for representing server capability.
 * only define supported capability
 */
struct ServerCapabilities {
    Union<TextDocumentSyncOptions> textDocumentSync;    // optional
    bool hoverProvider{false};
    Union<CompletionOptions> completionProvider;    // optional
    Union<SignatureHelpOptions> signatureHelpProvider;  // optiona;
    bool definitionProvider{false};
    bool referencesProvider{false};
    bool documentHighlightProvider{false};
    bool documentSymbolProvider{false};
    bool workspaceSymbolProvider{false};
    Union<bool, CodeActionOptions> codeActionProvider;  // optional
    Union<CodeLensOptions> codeLensProvider;    // optional
    bool documentFormattingProvider{false};
    bool documentRangeFormattingProvider{false};
    Union<DocumentOnTypeFormattingOptions> documentOnTypeFormattingProvider;    // optional
    Union<bool, RenameOptions> renameProvider;  // optional
    Union<DocumentLinkOptions> documentLinkProvider;    // optional
    Union<ExecuteCommandOptions> executeCommandProvider;    // optional
};

struct InitializeResult {
    ServerCapabilities capabilities;
};

} // namespace lsp

namespace rpc {

using namespace lsp;

inline bool isType(const JSON &value, TypeHolder<DocumentURI>) {
    return value.isString();
}
void fromJSON(JSON &&json, DocumentURI &uri);
JSON toJSON(const DocumentURI &uri);

void fromJSON(JSON &&json, Position &p);
JSON toJSON(const Position &p);

void fromJSON(JSON &&json, Range &range);
JSON toJSON(const Range &range);

void fromJSON(JSON &&json, Location &location);
JSON toJSON(const Location &location);

void fromJSON(JSON &&json, LocationLink &link);
JSON toJSON(const LocationLink &link);

void fromJSON(JSON &&json, DiagnosticSeverity &severity);
JSON toJSON(DiagnosticSeverity severity);

void fromJSON(JSON &&json, DiagnosticRelatedInformation &info);
JSON toJSON(const DiagnosticRelatedInformation &info);

void fromJSON(JSON &&json, Diagnostic &diagnostic);
JSON toJSON(const Diagnostic &diagnostic);

void fromJSON(JSON &&json, Command &command);
JSON toJSON(const Command &command);

void fromJSON(JSON &&json, TextEdit &edit);
JSON toJSON(const TextEdit &edit);

void fromJSON(JSON &&json, TraceSetting &setting);
JSON toJSON(TraceSetting setting);

void fromJSON(JSON &&json, ClientCapabilities &cap);
JSON toJSON(const ClientCapabilities &cap);

void fromJSON(JSON &&json, InitializeParams &params);
JSON toJSON(const InitializeParams &params);

void fromJSON(JSON &&json, TextDocumentSyncKind &kind);
JSON toJSON(TextDocumentSyncKind kind);

void fromJSON(JSON &&json, CompletionOptions &options);
JSON toJSON(const CompletionOptions &options);

void fromJSON(JSON &&json, SignatureHelpOptions &options);
JSON toJSON(const SignatureHelpOptions &options);

void fromJSON(JSON &&json, CodeActionKind &kind);
JSON toJSON(const CodeActionKind &kind);

inline bool isType(const JSON &value, TypeHolder<CodeActionOptions>) {
    return value.isObject();    //FIXME:
}
void fromJSON(JSON &&json, CodeActionOptions &options);
JSON toJSON(const CodeActionOptions &options);

void fromJSON(JSON &&json, CodeLensOptions &options);
JSON toJSON(const CodeLensOptions &options);

void fromJSON(JSON &&json, DocumentOnTypeFormattingOptions &options);
JSON toJSON(const DocumentOnTypeFormattingOptions &options);

inline bool isType(const JSON &value, TypeHolder<RenameOptions>) {
    return value.isObject();    //FIXME:
}
void fromJSON(JSON &&json, RenameOptions &options);
JSON toJSON(const RenameOptions &options);

void fromJSON(JSON &&json, DocumentLinkOptions &options);
JSON toJSON(const DocumentLinkOptions &options);

void fromJSON(JSON &&json, ExecuteCommandOptions &options);
JSON toJSON(const ExecuteCommandOptions &options);

void fromJSON(JSON &&json, SaveOptions &options);
JSON toJSON(const SaveOptions &options);

inline bool isType(const JSON &value, TypeHolder<TextDocumentSyncOptions>) {
    return value.isObject();    //FIXME:
}
void fromJSON(JSON &&json, TextDocumentSyncOptions &options);
JSON toJSON(const TextDocumentSyncOptions &options);

void fromJSON(JSON &&json, StaticRegistrationOptions &options);
JSON toJSON(const StaticRegistrationOptions &options);

void fromJSON(JSON &&json, ServerCapabilities &cap);
JSON toJSON(const ServerCapabilities &cap);

void fromJSON(JSON &&json, InitializeResult &ret);
JSON toJSON(const InitializeResult &ret);


} // namespace rpc
} // namespace ydsh

#endif //YDSH_TOOLS_LSP_H
