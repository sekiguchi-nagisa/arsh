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
  Position start;
  Position end;

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
  //    std::string code; // string | number, //FIXME: currently not supported.
  //    std::string source;                   //FIXME: currently not supported.
  std::string message;
  Optional<std::vector<DiagnosticRelatedInformation>> relatedInformation; // optional

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(range);
    JSONIFY(severity);
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

struct TextDocumentClientCapabilities {
  Optional<PublishDiagnosticsClientCapabilities> publishDiagnostics;

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(publishDiagnostics);
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

struct CompletionOptions {
  Optional<bool> resolveProvider;                       // optional
  Optional<std::vector<std::string>> triggerCharacters; // optional

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(resolveProvider);
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
  Optional<bool> openClose;                // optional
  Optional<TextDocumentSyncKind> change;   // optional
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

/**
 * for representing server capability.
 * only define supported capability
 */
struct ServerCapabilities {
  Optional<TextDocumentSyncOptions> textDocumentSync; // optional
  bool hoverProvider{false};
  Optional<CompletionOptions> completionProvider; // optional
  bool definitionProvider{false};
  bool referencesProvider{false};
  bool documentHighlightProvider{false};
  bool documentSymbolProvider{false};
  bool workspaceSymbolProvider{false};
  bool documentFormattingProvider{false};
  bool documentRangeFormattingProvider{false};

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(textDocumentSync);
    JSONIFY(hoverProvider);
    JSONIFY(completionProvider);
    JSONIFY(definitionProvider);
    JSONIFY(referencesProvider);
    JSONIFY(documentHighlightProvider);
    JSONIFY(documentSymbolProvider);
    JSONIFY(workspaceSymbolProvider);
    JSONIFY(documentFormattingProvider);
    JSONIFY(documentRangeFormattingProvider);
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

struct HoverParams : public TextDocumentPositionParams, public WorkDoneProgressParams {
  template <typename T>
  void jsonify(T &t) {
    TextDocumentPositionParams::jsonify(t);
    WorkDoneProgressParams::jsonify(t);
  }
};

#define EACH_MARKUP_KIND(OP)                                                                       \
  OP(PlainText, "plaintext")                                                                       \
  OP(Markdown, "markdown")

enum class MarkupKind : unsigned int {
#define GEN_ENUM(E, S) E,
  EACH_MARKUP_KIND(GEN_ENUM)
#undef GEN_ENUM
};

const char *toString(const MarkupKind &kind);

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

struct CompletionItem {
  std::string label;
  Optional<CompletionItemKind> kind;
  Optional<std::string> sortText;
  int priority; // dummy. not defined in lsp

  template <typename T>
  void jsonify(T &t) {
    JSONIFY(label);
    JSONIFY(kind);
    JSONIFY(sortText);
  }
};

#undef JSONIFY

} // namespace ydsh::lsp

#endif // YDSH_TOOLS_ANALYZER_LSP_H
