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
#include "../json/serialize.h"
#include "../uri/uri.h"

namespace ydsh::lsp {

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

#define JSONIFIY(m) t(#m, m)

using DocumentURI = std::string;

struct Position {
    int line{0};
    int character{0};

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(line);
        JSONIFIY(character);
    }
};

struct Range {
    Position start;
    Position end;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(start);
        JSONIFIY(end);
    }
};

struct Location {
    DocumentURI uri;
    Range range;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(uri);
        JSONIFIY(range);
    }
};

struct LocationLink {
    Optional<Range> originSelectionRange;  // optional
    std::string targetUri;
    Range targetRange;
    Optional<Range> targetSelectionRange;  // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(originSelectionRange);
        JSONIFIY(targetUri);
        JSONIFIY(targetRange);
        JSONIFIY(targetSelectionRange);
    }
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

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(location);
        JSONIFIY(message);
    }
};

struct Diagnostic {
    Range range;
    Optional<DiagnosticSeverity> severity; // optional
//    std::string code; // string | number, //FIXME: currently not supported.
//    std::string source;                   //FIXME: currently not supported.
    std::string message;
    Optional<std::vector<DiagnosticRelatedInformation>> relatedInformation;   // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(range);
        JSONIFIY(severity);
        JSONIFIY(message);
        JSONIFIY(relatedInformation);
    }
};

struct Command {
    std::string title;
    std::string command;
//    std::vector<JSON> arguments // any[], optional  //FIXME: currently not supported.

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(title);
        JSONIFIY(command);
    }
};

struct TextEdit {
    Range range;
    std::string newText;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(range);
        JSONIFIY(newText);
    }
};

// for Initialize request

struct ClientCapabilities {
    Optional<JSON> workspace;   // optional
    Optional<JSON> textDocument; // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(workspace);
        JSONIFIY(textDocument);
    }
};

#define EACH_TRACE_SETTING(OP) \
    OP(off) \
    OP(message) \
    OP(verbose)

enum class TraceSetting : unsigned int {
#define GEN_ENUM(e) e,
    EACH_TRACE_SETTING(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(TraceSetting setting);

bool toEnum(const char *str, TraceSetting &setting);

template <typename T>
void jsonify(T &t, TraceSetting &setting) {
    if constexpr(is_serialize_v<T>) {
        std::string value = toString(setting);
        t(value);
    } else if constexpr(is_deserialize_v<T>) {
        std::string value;
        t(value);
        t.hasError() || toEnum(value.c_str(), setting);
    }
}

struct InitializeParams {
    Union<int, std::nullptr_t> processId{nullptr};
    Optional<Union<std::string, std::nullptr_t>> rootPath;    // optional
    Union<DocumentURI, std::nullptr_t> rootUri{nullptr};
    Optional<JSON> initializationOptions; // optional
    ClientCapabilities capabilities;
    Optional<TraceSetting> trace;  // optional
//    Union<WorkspaceFolder, std::nullptr_t> workspaceFolders;    // optional   //FIXME: currently not supported

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(processId);
        JSONIFIY(rootPath);
        JSONIFIY(rootUri);
        JSONIFIY(initializationOptions);
        JSONIFIY(capabilities);
        JSONIFIY(trace);
    }
};

// for server capability
enum class TextDocumentSyncKind : int {
    None = 0,
    Full = 1,
    Incremental = 2,
};

struct CompletionOptions {
    Optional<bool> resolveProvider;    // optional
    Optional<std::vector<std::string>> triggerCharacters;  // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(resolveProvider);
        JSONIFIY(triggerCharacters);
    }
};

struct SignatureHelpOptions {
    Optional<std::vector<std::string>> triggerCharacters;  // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(triggerCharacters);
    }
};

#define EACH_CODE_ACTION_KIND(OP) \
    OP(Empty, "") \
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

const char *toString(CodeActionKind kind);

bool toEnum(const char *str, CodeActionKind &kind);

template <typename T>
void jsonify(T &t, CodeActionKind &kind) {
    if constexpr(is_serialize_v<T>) {
        std::string value = toString(kind);
        t(value);
    } else if constexpr(is_deserialize_v<T>) {
        std::string value;
        t(value);
        t.hasError() || toEnum(value.c_str(), kind);
    }
}

struct CodeActionOptions {
    Optional<std::vector<CodeActionKind>> codeActionKinds; // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(codeActionKinds);
    }
};

struct CodeLensOptions {
    Optional<bool> resolveProvider;    // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(resolveProvider);
    }
};

struct DocumentOnTypeFormattingOptions {
    std::string firstTriggerCharacter;
    Optional<std::vector<std::string>> moreTriggerCharacter;   // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(firstTriggerCharacter);
        JSONIFIY(moreTriggerCharacter);
    }
};

struct RenameOptions {
    Optional<bool> prepareProvider;    // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(prepareProvider);
    }
};

struct DocumentLinkOptions {
    Optional<bool> resolveProvider;    // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(resolveProvider);
    }
};

struct ExecuteCommandOptions {
    std::vector<std::string> commands;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(commands);
    }
};

struct SaveOptions {
    Optional<bool> includeText;    // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(includeText);
    }
};

struct ColorProviderOptions {};
struct FoldingRangeProviderOptions {};

struct TextDocumentSyncOptions {
    Optional<bool> openClose;  // optional
    Optional<TextDocumentSyncKind> change; // optional
    Optional<bool> willSave;   // optional
    Optional<bool> willSaveWaitUntil;  // optional
    Optional<Union<bool, SaveOptions>> save;    // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(openClose);
        JSONIFIY(change); //FIXME:
        JSONIFIY(willSave);
        JSONIFIY(willSaveWaitUntil);
        JSONIFIY(save);
    }
};

struct StaticRegistrationOptions {
    Optional<std::string> id;  // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(id);
    }
};

/**
 * for representing server capability.
 * only define supported capability
 */
struct ServerCapabilities {
    Optional<TextDocumentSyncOptions> textDocumentSync;    // optional
    bool hoverProvider{false};
    Optional<CompletionOptions> completionProvider;    // optional
    Optional<SignatureHelpOptions> signatureHelpProvider;  // optiona;
    bool definitionProvider{false};
    bool referencesProvider{false};
    bool documentHighlightProvider{false};
    bool documentSymbolProvider{false};
    bool workspaceSymbolProvider{false};
    Optional<Union<bool, CodeActionOptions>> codeActionProvider;  // optional
    Optional<CodeLensOptions> codeLensProvider;    // optional
    bool documentFormattingProvider{false};
    bool documentRangeFormattingProvider{false};
    Optional<DocumentOnTypeFormattingOptions> documentOnTypeFormattingProvider;    // optional
    Optional<Union<bool, RenameOptions>> renameProvider;  // optional
    Optional<DocumentLinkOptions> documentLinkProvider;    // optional
    Optional<ExecuteCommandOptions> executeCommandProvider;    // optional

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(textDocumentSync);
        JSONIFIY(hoverProvider);
        JSONIFIY(completionProvider);
        JSONIFIY(signatureHelpProvider);
        JSONIFIY(definitionProvider);
        JSONIFIY(referencesProvider);
        JSONIFIY(documentHighlightProvider);
        JSONIFIY(documentSymbolProvider);
        JSONIFIY(workspaceSymbolProvider);
        JSONIFIY(codeActionProvider);
        JSONIFIY(codeLensProvider);
        JSONIFIY(documentFormattingProvider);
        JSONIFIY(documentRangeFormattingProvider);
        JSONIFIY(documentOnTypeFormattingProvider);
        JSONIFIY(renameProvider);
        JSONIFIY(documentLinkProvider);
        JSONIFIY(executeCommandProvider);
    }
};

struct InitializeResult {
    ServerCapabilities capabilities;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(capabilities);
    }
};

struct InitializedParams {
    template <typename T>
    void jsonify(T &) {}
};

// for TextDocument

struct TextDocumentIdentifier {
    DocumentURI uri;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(uri);
    }
};

struct VersionedTextDocumentIdentifier : public TextDocumentIdentifier {
    int version;

    template <typename T>
    void jsonify(T &t) {
        t(static_cast<TextDocumentIdentifier&>(*this));
        JSONIFIY(version);
    }
};

struct TextDocumentItem {
    DocumentURI uri;
    std::string languageId;
    int version;
    std::string text;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(uri);
        JSONIFIY(languageId);
        JSONIFIY(version);
        JSONIFIY(text);
    }
};

struct DidOpenTextDocumentParams {
    TextDocumentItem textDocument;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(textDocument);
    }
};

struct DidCloseTextDocumentParams {
    TextDocumentIdentifier textDocument;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(textDocument);
    }
};

struct TextDocumentContentChangeEvent {
    Optional<Range> range;  // if invalid, text is considered full content of document
    Optional<unsigned int> rangeLength; // deprecated
    std::string text;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(range);
        JSONIFIY(rangeLength);
        JSONIFIY(text);
    }
};

struct DidChangeTextDocumentParams {
    VersionedTextDocumentIdentifier textDocument;
    std::vector<TextDocumentContentChangeEvent> contentChanges;

    template <typename T>
    void jsonify(T &t) {
        JSONIFIY(textDocument);
        JSONIFIY(contentChanges);
    }
};


} // namespace ydsh::lsp

#endif //YDSH_TOOLS_LSP_H
