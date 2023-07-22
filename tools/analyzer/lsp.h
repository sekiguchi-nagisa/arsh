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

#ifndef YDSH_TOOLS_ANALYZER_LSP_H
#define YDSH_TOOLS_ANALYZER_LSP_H

#include <misc/logger_base.hpp>

#include "../json/json.h"
#include "../json/serialize.h"
#include "../uri/uri.h"

/**
 * LSP definition 3.16
 */
namespace ydsh::lsp {

using namespace json;

// definition of basic interface of language server protocol
// LSP specific error code
enum LSPErrorCode : int {
  ServerErrorStart = -32099,
  ServerErrorEnd = -32000,
  ServerNotInitialized = -32002,
  UnknownErrorCode = -32001,
  RequestCancelled = -32800,
  ContentModified = -32801,
};

#define JSONIFY(m) t(#m, m)

using DocumentURI = std::string;

/**
 * line and character are 0-based and based on UTF16 encoding
 * (encoding of actual content is UTF8)
 */
struct Position {
  int line{0};
  int character{0};

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(line);
    JSONIFY(character);
  }

  std::string toString() const;
};

struct Range {
  Position start; // inclusive
  Position end;   // exclusive

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(start);
    JSONIFY(end);
  }

  std::string toString() const;
};

struct Location {
  DocumentURI uri;
  Range range;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(uri);
    JSONIFY(range);
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
    JSONIFY(location);
    JSONIFY(message);
  }
};

struct Diagnostic {
  Range range;
  Optional<DiagnosticSeverity> severity; // optional
  std::string code;
  std::string message;
  Optional<std::vector<DiagnosticRelatedInformation>> relatedInformation; // optional

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(range);
    JSONIFY(severity);
    JSONIFY(code);
    JSONIFY(message);
    JSONIFY(relatedInformation);
  }
};

// for Initialize request
struct PublishDiagnosticsClientCapabilities {
  Optional<bool> versionSupport;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(versionSupport);
  }
};

#define EACH_MARKUP_KIND(OP)                                                                       \
  OP(PlainText, "plaintext")                                                                       \
  OP(Markdown, "markdown")

enum class MarkupKind : unsigned char {
#define GEN_ENUM(E, S) E,
  EACH_MARKUP_KIND(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(MarkupKind kind);

bool toEnum(const char *str, MarkupKind &kind);

template <typename T>
void jsonify(T &t, MarkupKind &kind) {
  if constexpr (is_serialize_v<T>) {
    std::string value = toString(kind);
    t(value);
  } else if constexpr (is_deserialize_v<T>) {
    std::string value;
    t(value);
    t.hasError() || toEnum(value.c_str(), kind);
  }
}

struct HoverClientCapabilities {
  Optional<std::vector<MarkupKind>> contentFormat;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(contentFormat);
  }
};

#define EACH_SEMANTIC_TOKEN_TYPES(OP)                                                              \
  /*OP(namespace_, "namespace")  */                                                                \
  OP(type_, "type")                                                                                \
  /*OP(class_, "class")*/                                                                          \
  /*OP(enum_, "enum") */                                                                           \
  /*OP(interface_, "interface")*/                                                                  \
  OP(struct_, "struct")                                                                            \
  /*OP(typeParameter_, "typeParameter")*/                                                          \
  OP(parameter_, "parameter")                                                                      \
  OP(variable_, "variable")                                                                        \
  OP(property_, "property")                                                                        \
  /*OP(enumMember_, "enumMember")*/                                                                \
  OP(event_, "event")                                                                              \
  OP(function_, "function")                                                                        \
  OP(method_, "method")                                                                            \
  /*OP(macro_, "macro") */                                                                         \
  OP(keyword_, "keyword")                                                                          \
  OP(modifier_, "modifier")                                                                        \
  OP(comment_, "comment")                                                                          \
  OP(string_, "string")                                                                            \
  OP(number_, "number")                                                                            \
  OP(regexp_, "regexp")                                                                            \
  OP(operator_, "operator")

enum class SemanticTokenTypes : unsigned int {
#define GEN_ENUM(E, V) E,
  EACH_SEMANTIC_TOKEN_TYPES(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(SemanticTokenTypes type);

template <typename T>
void jsonify(T &t, SemanticTokenTypes &type) {
  if constexpr (is_serialize_v<T>) {
    std::string value = toString(type);
    t(value);
  } else {
    static_assert("unsupported");
  }
}

#define EACH_SEMANTIC_TOKEN_MODIFIERS(OP)                                                          \
  /*OP(declaration_, "declaration")*/                                                              \
  OP(definition_, "definition")                                                                    \
  OP(readonly_, "readonly")                                                                        \
  /*OP(static_, "static") */                                                                       \
  /*OP(deprecated_, "deprecated")*/                                                                \
  /*OP(abstract_, "abstract") */                                                                   \
  /*OP(async_, "async") */                                                                         \
  /*OP(modification_, "modification") */                                                           \
  /*OP(documentation_, "documentation")*/                                                          \
  OP(defaultLibrary_, "defaultLibrary")

enum class SemanticTokenModifiers : unsigned int {
#define GEN_ENUM(E, V) E,
  EACH_SEMANTIC_TOKEN_MODIFIERS(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(SemanticTokenModifiers modifier);

template <typename T>
void jsonify(T &t, SemanticTokenModifiers &modifier) {
  if constexpr (is_serialize_v<T>) {
    std::string value = toString(modifier);
    t(value);
  } else {
    static_assert("unsupported");
  }
}

#define EACH_TOKEN_FORMAT(OP) OP(Relative, "relative")

enum class TokenFormat : unsigned int {
#define GEN_ENUM(E, S) E,
  EACH_TOKEN_FORMAT(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(TokenFormat format);

bool toEnum(const char *str, TokenFormat &format);

template <typename T>
void jsonify(T &t, TokenFormat &format) {
  if constexpr (is_serialize_v<T>) {
    std::string value = toString(format);
    t(value);
  } else if constexpr (is_deserialize_v<T>) {
    std::string value;
    t(value);
    t.hasError() || toEnum(value.c_str(), format);
  }
}

struct SemanticTokensLegend {
  std::vector<SemanticTokenTypes> tokenTypes;
  std::vector<SemanticTokenModifiers> tokenModifiers;

  static SemanticTokensLegend create();

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(tokenTypes);
    JSONIFY(tokenModifiers);
  }
};

struct SemanticTokensClientCapabilities {
  std::vector<SemanticTokenTypes> tokenTypes;
  std::vector<SemanticTokenModifiers> tokenModifiers;
  std::vector<TokenFormat> formats;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(tokenTypes);
    JSONIFY(tokenModifiers);
    JSONIFY(formats);
  }
};

struct DocumentLinkClientCapabilities {
  template <typename T>
  void jsonify(T &) {}
};

struct CompletionClientCapabilities {
  struct CompletionItem {
    Optional<bool> labelDetailsSupport;

    template <typename T>
    void jsonify(T &t) {
      JSONIFY(labelDetailsSupport);
    }
  };

  Optional<CompletionItem> completionItem;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(completionItem);
  }
};

struct TextDocumentClientCapabilities {
  Optional<PublishDiagnosticsClientCapabilities> publishDiagnostics;
  Optional<HoverClientCapabilities> hover;
  Optional<DocumentLinkClientCapabilities> documentLink;
  Optional<SemanticTokensClientCapabilities> semanticTokens;
  Optional<CompletionClientCapabilities> completion;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(publishDiagnostics);
    JSONIFY(hover);
    JSONIFY(documentLink);
    JSONIFY(semanticTokens);
    JSONIFY(completion);
  }
};

struct ClientCapabilities {
  Optional<JSON> workspace;                              // optional
  Optional<TextDocumentClientCapabilities> textDocument; // optional

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(workspace);
    JSONIFY(textDocument);
  }
};

#define EACH_TRACE_VALUE(OP)                                                                       \
  OP(off)                                                                                          \
  OP(message)                                                                                      \
  OP(verbose)

enum class TraceValue : unsigned char {
#define GEN_ENUM(e) e,
  EACH_TRACE_VALUE(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(TraceValue setting);

bool toEnum(const char *str, TraceValue &setting);

template <typename T>
void jsonify(T &t, TraceValue &setting) {
  if constexpr (is_serialize_v<T>) {
    std::string value = toString(setting);
    t(value);
  } else if constexpr (is_deserialize_v<T>) {
    std::string value;
    t(value);
    t.hasError() || toEnum(value.c_str(), setting);
  }
}

struct SetTraceParams {
  TraceValue value;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(value);
  }
};

using ProgressToken = Union<int, std::string>;

struct WorkDoneProgressParams {
  Optional<ProgressToken> workDoneToken;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(workDoneToken);
  }
};

struct InitializeParams : public WorkDoneProgressParams {
  Union<int, std::nullptr_t> processId{nullptr};
  Optional<Union<std::string, std::nullptr_t>> rootPath; // optional
  Union<DocumentURI, std::nullptr_t> rootUri{nullptr};
  Optional<JSON> initializationOptions; // optional
  ClientCapabilities capabilities;
  Optional<TraceValue> trace; // optional

  template <typename T>
  void jsonify(T &t) {
    WorkDoneProgressParams::jsonify(t);
    JSONIFY(processId);
    JSONIFY(rootPath);
    JSONIFY(rootUri);
    JSONIFY(initializationOptions);
    JSONIFY(capabilities);
    JSONIFY(trace);
  }
};

// for server capability
enum class TextDocumentSyncKind : int {
  None = 0,
  Full = 1,
  Incremental = 2,
};

struct WorkDoneProgressOptions {
  Optional<bool> workDoneProgress;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(workDoneProgress);
  }
};

struct CompletionOptions : public WorkDoneProgressOptions {
  struct CompletionItem {
    bool labelDetailsSupport{true};

    template <typename T>
    void jsonify(T &t) {
      JSONIFY(labelDetailsSupport);
    }
  };

  Optional<bool> resolveProvider; // optional
  std::vector<std::string> triggerCharacters;
  CompletionItem completionItem;

  template <typename T>
  void jsonify(T &t) {
    WorkDoneProgressOptions::jsonify(t);
    JSONIFY(resolveProvider);
    JSONIFY(triggerCharacters);
    JSONIFY(completionItem);
  }
};

struct SignatureHelpOptions : public WorkDoneProgressOptions {
  std::vector<std::string> triggerCharacters;

  template <typename T>
  void jsonify(T &t) {
    WorkDoneProgressOptions::jsonify(t);
    JSONIFY(triggerCharacters);
  }
};

struct SaveOptions {
  Optional<bool> includeText; // optional

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(includeText);
  }
};

struct TextDocumentSyncOptions {
  bool openClose{true};
  TextDocumentSyncKind change{TextDocumentSyncKind::Incremental};
  Optional<bool> willSave;                 // optional
  Optional<bool> willSaveWaitUntil;        // optional
  Optional<Union<bool, SaveOptions>> save; // optional

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(openClose);
    JSONIFY(change);
    JSONIFY(willSave);
    JSONIFY(willSaveWaitUntil);
    JSONIFY(save);
  }
};

struct SemanticTokensOptions : public WorkDoneProgressOptions {
  SemanticTokensLegend legend;
  Optional<bool> full;

  template <typename T>
  void jsonify(T &t) {
    WorkDoneProgressOptions::jsonify(t);
    JSONIFY(legend);
    JSONIFY(full);
  }
};

struct DocumentLinkOptions : public WorkDoneProgressOptions {
  bool resolveProvider{false};

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(resolveProvider);
  }
};

/**
 * for representing server capability.
 * only define supported capability
 */
struct ServerCapabilities {
  TextDocumentSyncOptions textDocumentSync;
  SignatureHelpOptions signatureHelpProvider;
  bool hoverProvider{true};
  CompletionOptions completionProvider;
  bool definitionProvider{true};
  bool referencesProvider{true};
  bool documentHighlightProvider{true};
  bool documentSymbolProvider{true};
  bool workspaceSymbolProvider{false};
  bool documentFormattingProvider{false};
  bool documentRangeFormattingProvider{false};
  DocumentLinkOptions documentLinkProvider;
  SemanticTokensOptions semanticTokensProvider;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(textDocumentSync);
    JSONIFY(hoverProvider);
    JSONIFY(signatureHelpProvider);
    JSONIFY(completionProvider);
    JSONIFY(definitionProvider);
    JSONIFY(referencesProvider);
    JSONIFY(documentHighlightProvider);
    JSONIFY(documentSymbolProvider);
    JSONIFY(workspaceSymbolProvider);
    JSONIFY(documentFormattingProvider);
    JSONIFY(documentRangeFormattingProvider);
    JSONIFY(documentLinkProvider);
    JSONIFY(semanticTokensProvider);
  }
};

struct InitializeResult {
  ServerCapabilities capabilities;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(capabilities);
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
    JSONIFY(uri);
  }
};

struct VersionedTextDocumentIdentifier : public TextDocumentIdentifier {
  int version;

  template <typename T>
  void jsonify(T &t) {
    TextDocumentIdentifier::jsonify(t);
    JSONIFY(version);
  }
};

struct TextDocumentItem {
  DocumentURI uri;
  std::string languageId;
  int version;
  std::string text;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(uri);
    JSONIFY(languageId);
    JSONIFY(version);
    JSONIFY(text);
  }
};

struct TextDocumentPositionParams {
  TextDocumentIdentifier textDocument;
  Position position;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(textDocument);
    JSONIFY(position);
  }
};

struct PartialResultParams {
  Optional<ProgressToken> partialResultToken;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(partialResultToken);
  }
};

struct DidOpenTextDocumentParams {
  TextDocumentItem textDocument;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(textDocument);
  }
};

struct DidCloseTextDocumentParams {
  TextDocumentIdentifier textDocument;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(textDocument);
  }
};

struct DefinitionParams : public TextDocumentPositionParams,
                          public WorkDoneProgressParams,
                          public PartialResultParams {
  template <typename T>
  void jsonify(T &t) {
    TextDocumentPositionParams::jsonify(t);
    WorkDoneProgressParams::jsonify(t);
    PartialResultParams::jsonify(t);
  }
};

struct ReferenceContext {
  bool includeDeclaration{false};

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(includeDeclaration);
  }
};

struct ReferenceParams : public TextDocumentPositionParams,
                         public WorkDoneProgressParams,
                         public PartialResultParams {
  ReferenceContext context;

  template <typename T>
  void jsonify(T &t) {
    TextDocumentPositionParams::jsonify(t);
    WorkDoneProgressParams::jsonify(t);
    PartialResultParams::jsonify(t);
    JSONIFY(context);
  }
};

struct DocumentHighlightParams : public TextDocumentPositionParams,
                                 public WorkDoneProgressParams,
                                 public PartialResultParams {
  template <typename T>
  void jsonify(T &t) {
    TextDocumentPositionParams::jsonify(t);
    WorkDoneProgressParams::jsonify(t);
    PartialResultParams::jsonify(t);
  }
};

enum class DocumentHighlightKind : int {
  Text = 1, // default
  Read = 2,
  Write = 3,
};

struct DocumentHighlight {
  Range range;
  DocumentHighlightKind kind;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(range);
    JSONIFY(kind);
  }
};

struct HoverParams : public TextDocumentPositionParams, public WorkDoneProgressParams {
  template <typename T>
  void jsonify(T &t) {
    TextDocumentPositionParams::jsonify(t);
    WorkDoneProgressParams::jsonify(t);
  }
};

struct MarkupContent {
  MarkupKind kind;
  std::string value;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(kind);
    JSONIFY(value);
  }
};

struct Hover {
  MarkupContent contents;
  Optional<Range> range;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(contents);
    JSONIFY(range);
  }
};

struct TextDocumentContentChangeEvent {
  Optional<Range> range;              // if invalid, text is considered full content of document
  Optional<unsigned int> rangeLength; // deprecated
  std::string text;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(range);
    JSONIFY(rangeLength);
    JSONIFY(text);
  }
};

struct DidChangeTextDocumentParams {
  VersionedTextDocumentIdentifier textDocument;
  std::vector<TextDocumentContentChangeEvent> contentChanges;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(textDocument);
    JSONIFY(contentChanges);
  }
};

struct PublishDiagnosticsParams {
  DocumentURI uri;
  Optional<int> version;
  std::vector<Diagnostic> diagnostics;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(uri);
    JSONIFY(version);
    JSONIFY(diagnostics);
  }
};

enum class CompletionTriggerKind : int {
  Invoked = 1,
  TriggerCharacter = 2,
  TriggerForIncompleteCompletions = 3,
};

struct CompletionContext {
  CompletionTriggerKind triggerKind;
  Optional<std::string> triggerCharacter;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(triggerKind);
    JSONIFY(triggerCharacter);
  }
};

struct CompletionParams : public TextDocumentPositionParams,
                          public WorkDoneProgressParams,
                          public PartialResultParams {
  Optional<CompletionContext> context;

  template <typename T>
  void jsonify(T &t) {
    TextDocumentPositionParams::jsonify(t);
    WorkDoneProgressParams::jsonify(t);
    PartialResultParams::jsonify(t);
    JSONIFY(context);
  }
};

enum class CompletionItemKind : unsigned int {
  Text = 1,
  Method = 2,
  Function = 3,
  Constructor = 4,
  Field = 5,
  Variable = 6,
  Class = 7,
  Interface = 8,
  Module = 9,
  Property = 10,
  Unit = 11,
  Value = 12,
  Enum = 13,
  Keyword = 14,
  Snippet = 15,
  Color = 16,
  File = 17,
  Reference = 18,
  Folder = 19,
  EnumMember = 20,
  Constant = 21,
  Struct = 22,
  Event = 23,
  Operator = 24,
  TypeParameter = 25,
};

struct CompletionItemLabelDetails {
  Optional<std::string> detail;
  Optional<std::string> description;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(detail);
    JSONIFY(description);
  }
};

struct CompletionItem {
  std::string label;
  Optional<CompletionItemLabelDetails> labelDetails;
  CompletionItemKind kind;
  Optional<std::string> sortText;
  int priority; // dummy. not defined in lsp

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(label);
    JSONIFY(labelDetails);
    JSONIFY(kind);
    JSONIFY(sortText);
  }
};

#define EACH_COMMAND_COMPLETION_KIND(OP)                                                           \
  OP(disabled_, "disabled")                                                                        \
  OP(default_, "default")                                                                          \
  OP(all_, "all")

enum class CmdCompKind : unsigned char {
#define GEN_ENUM(E, V) E,
  EACH_COMMAND_COMPLETION_KIND(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(CmdCompKind kind);

bool toEnum(const char *str, CmdCompKind &kind);

template <typename T>
void jsonify(T &t, CmdCompKind &kind) {
  if constexpr (is_serialize_v<T>) {
    std::string value = toString(kind);
    t(value);
  } else if constexpr (is_deserialize_v<T>) {
    std::string value;
    t(value);
    t.hasError() || toEnum(value.c_str(), kind);
  }
}

#define EACH_BINARY_FLAG(OP)                                                                       \
  OP(enabled, "enabled")                                                                           \
  OP(disabled, "disabled")

enum class BinaryFlag : unsigned char {
#define GEN_ENUM(E, V) E,
  EACH_BINARY_FLAG(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(BinaryFlag kind);

bool toEnum(const char *str, BinaryFlag &kind);

template <typename T>
void jsonify(T &t, BinaryFlag &kind) {
  if constexpr (is_serialize_v<T>) {
    std::string value = toString(kind);
    t(value);
  } else if constexpr (is_deserialize_v<T>) {
    std::string value;
    t(value);
    t.hasError() || toEnum(value.c_str(), kind);
  }
}

struct ConfigSetting {
  Optional<Union<LogLevel, JSON>> logLevel;
  Optional<Union<CmdCompKind, JSON>> commandCompletion;
  Optional<Union<BinaryFlag, JSON>> commandArgumentCompletion;
  Optional<Union<BinaryFlag, JSON>> semanticHighlight;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(logLevel);
    JSONIFY(commandCompletion);
    JSONIFY(commandArgumentCompletion);
    JSONIFY(semanticHighlight);
  }
};

struct ConfigSettingWrapper {
  Optional<Union<ConfigSetting, JSON>> ydshd;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(ydshd);
  }
};

struct DidChangeConfigurationParams {
  Optional<Union<ConfigSettingWrapper, JSON>> settings;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(settings);
  }
};

struct SemanticTokensParams : public WorkDoneProgressParams, public PartialResultParams {
  TextDocumentIdentifier textDocument;

  template <typename T>
  void jsonify(T &t) {
    WorkDoneProgressParams::jsonify(t);
    PartialResultParams::jsonify(t);
    JSONIFY(textDocument);
  }
};

struct SemanticTokens {
  Optional<std::string> resultId;
  std::vector<unsigned int> data;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(resultId);
    JSONIFY(data);
  }
};

struct DocumentLinkParams : public WorkDoneProgressParams, public PartialResultParams {
  TextDocumentIdentifier textDocument;

  template <typename T>
  void jsonify(T &t) {
    WorkDoneProgressParams::jsonify(t);
    PartialResultParams::jsonify(t);
    JSONIFY(textDocument);
  }
};

struct DocumentLink {
  Range range;
  DocumentURI target;
  std::string tooltip;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(range);
    JSONIFY(target);
    JSONIFY(tooltip);
  }
};

struct DocumentSymbolParams : public WorkDoneProgressParams, public PartialResultParams {
  TextDocumentIdentifier textDocument;

  template <typename T>
  void jsonify(T &t) {
    WorkDoneProgressParams::jsonify(t);
    PartialResultParams::jsonify(t);
    JSONIFY(textDocument);
  }
};

enum class SymbolKind : unsigned int {
  File = 1,
  Module = 2,
  Namespace = 3,
  Package = 4,
  Class = 5,
  Method = 6,
  Property = 7,
  Field = 8,
  Constructor = 9,
  Enum = 10,
  Interface = 11,
  Function = 12,
  Variable = 13,
  Constant = 14,
  String = 15,
  Number = 16,
  Boolean = 17,
  Array = 18,
  Object = 19,
  Key = 20,
  Null = 21,
  EnumMember = 22,
  Struct = 23,
  Event = 24,
  Operator = 25,
  TypeParameter = 26,
};

struct DocumentSymbol {
  std::string name;
  Optional<std::string> detail;
  SymbolKind kind;
  Range range;
  Range selectionRange;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(name);
    JSONIFY(detail);
    JSONIFY(kind);
    JSONIFY(range);
    JSONIFY(selectionRange);
  }
};

enum class SignatureHelpTriggerKind : unsigned int {
  Invoked = 1,
  TriggerCharacter = 2,
  ContentChange = 3,
};

struct ParameterInformation {
  std::string label;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(label);
  }
};

struct SignatureInformation {
  std::string label;
  Optional<MarkupContent> documentation;
  Optional<std::vector<ParameterInformation>> parameters;
  Optional<unsigned int> activeParameter;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(label);
    JSONIFY(documentation);
    JSONIFY(parameters);
    JSONIFY(activeParameter);
  }
};

struct SignatureHelp {
  std::vector<SignatureInformation> signatures;
  Optional<unsigned int> activeSignature;
  Optional<unsigned int> activeParameter;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(signatures);
    JSONIFY(activeSignature);
    JSONIFY(activeParameter);
  }
};

struct SignatureHelpContext {
  SignatureHelpTriggerKind triggerKind;
  Optional<std::string> triggerCharacter;
  bool isRetrigger{false};
  //  Optional<SignatureHelp> activeSignatureHelp;  // unused

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(triggerKind);
    JSONIFY(triggerCharacter);
    JSONIFY(isRetrigger);
  }
};

struct SignatureHelpParams : public TextDocumentPositionParams, public WorkDoneProgressParams {
  Optional<SignatureHelpContext> context;

  template <typename T>
  void jsonify(T &t) {
    TextDocumentPositionParams::jsonify(t);
    WorkDoneProgressParams::jsonify(t);
    JSONIFY(context);
  }
};

#undef JSONIFY

} // namespace ydsh::lsp

namespace ydsh {

bool toEnum(const char *str, LogLevel &level);

template <typename T>
void jsonify(T &t, LogLevel &level) {
  if constexpr (json::is_serialize_v<T>) {
    std::string value = toString(level);
    t(value);
  } else if constexpr (json::is_deserialize_v<T>) {
    std::string value;
    t(value);
    t.hasError() || toEnum(value.c_str(), level);
  }
}

} // namespace ydsh

#endif // YDSH_TOOLS_ANALYZER_LSP_H
