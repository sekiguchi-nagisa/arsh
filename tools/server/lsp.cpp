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

#include "lsp.h"
#include "../json/conv.hpp"

namespace ydsh {
namespace rpc {

#define FROM_JSON(json, value, field) fromJSON(std::move(json[#field]), value.field)
#define MOVE_JSON(json, value, field) value.field = std::move(json[#field])

#define TO_MEMBER(obj, field) {#field, toJSON(obj.field)}


void fromJSON(JSON &&json, DocumentURI &uri) {
    fromJSON(std::move(json), uri.uri);
}

JSON toJSON(const DocumentURI &uri) {
    return JSON(uri.uri);
}


void fromJSON(JSON &&json, Position &p) {
    FROM_JSON(json, p, line);
    FROM_JSON(json, p, character);
}

JSON toJSON(const Position &p) {
    return {
        TO_MEMBER(p, line),
        TO_MEMBER(p, character)
    };
}


void fromJSON(JSON &&json, Range &range) {
    FROM_JSON(json, range, start);
    FROM_JSON(json, range, end);
}

JSON toJSON(const Range &range) {
    return {
        TO_MEMBER(range, start),
        TO_MEMBER(range, end)
    };
}


void fromJSON(JSON &&json, Location &location) {
    FROM_JSON(json, location, range);
    FROM_JSON(json, location, uri);
}

JSON toJSON(const Location &location) {
    return {
        TO_MEMBER(location, uri),
        TO_MEMBER(location, range)
    };
}


void fromJSON(JSON &&json, LocationLink &link) {
    FROM_JSON(json, link, originSelectionRange);
    FROM_JSON(json, link, targetUri);
    FROM_JSON(json, link, targetRange);
    FROM_JSON(json, link, targetSelectionRange);
}

JSON toJSON(const LocationLink &link) {
    return {
        TO_MEMBER(link, originSelectionRange),
        TO_MEMBER(link, targetUri),
        TO_MEMBER(link, targetRange),
        TO_MEMBER(link, targetSelectionRange)
    };
}

void fromJSON(JSON &&json, DiagnosticSeverity &severity) {
    severity = static_cast<DiagnosticSeverity>(json.asLong());
}

JSON toJSON(DiagnosticSeverity severity) {
    return JSON(static_cast<int>(severity));
}

void fromJSON(JSON &&json, DiagnosticRelatedInformation &info) {
    FROM_JSON(json, info, message);
    FROM_JSON(json, info, location);
}

JSON toJSON(const DiagnosticRelatedInformation &info) {
    return {
        TO_MEMBER(info, location),
        TO_MEMBER(info, message)
    };
}


void fromJSON(JSON &&json, Diagnostic &diagnostic) {
    FROM_JSON(json, diagnostic, range);
    FROM_JSON(json, diagnostic, severity);
    FROM_JSON(json, diagnostic, message);
    FROM_JSON(json, diagnostic, relatedInformation);
}

JSON toJSON(const Diagnostic &diagnostic) {
    return {
        TO_MEMBER(diagnostic, range),
        TO_MEMBER(diagnostic, severity),
        //{"code"}
        //{"source"}
        TO_MEMBER(diagnostic, message),
        TO_MEMBER(diagnostic, relatedInformation)
    };
}


void fromJSON(JSON &&json, Command &command) {
    FROM_JSON(json, command, title);
    FROM_JSON(json, command, command);
}

JSON toJSON(const Command &command) {
    return {
        TO_MEMBER(command, title),
        TO_MEMBER(command, command)
    };
}


void fromJSON(JSON &&json, TextEdit &edit) {
    FROM_JSON(json, edit, range);
    FROM_JSON(json, edit, newText);
}

JSON toJSON(const TextEdit &edit) {
    return {
        TO_MEMBER(edit, range),
        TO_MEMBER(edit, newText)
    };
}


void fromJSON(JSON &&json, TraceSetting &setting) {
    std::string value;
    fromJSON(std::move(json), value);
    setting = TraceSetting::off;
    if(value == "off") {
        setting = TraceSetting::off;
    } else if(value == "messages") {
        setting = TraceSetting::messages;
    } else if(value == "verbose") {
        setting = TraceSetting::verbose;
    }
}

JSON toJSON(TraceSetting setting) {
    JSON value;
    switch(setting) {
    case TraceSetting::off:
        value = JSON("off");
        break;
    case TraceSetting::messages:
        value = JSON("messages");
        break;
    case TraceSetting::verbose:
        value = JSON("verbose");
        break;
    }
    return value;
}


void fromJSON(JSON &&json, ClientCapabilities &cap) {
    MOVE_JSON(json, cap, workspace);
    MOVE_JSON(json, cap, textDocument);
}

JSON toJSON(const ClientCapabilities &cap) {
    return {
        TO_MEMBER(cap, workspace),
        TO_MEMBER(cap, textDocument)
    };
}


void fromJSON(JSON &&json, InitializeParams &params) {
    FROM_JSON(json, params, processId);
    FROM_JSON(json, params, rootPath);
    FROM_JSON(json, params, rootUri);
    MOVE_JSON(json, params, initializationOptions);
    FROM_JSON(json, params, capabilities);
    FROM_JSON(json, params, trace);
}

JSON toJSON(const InitializeParams &params) {
    return {
        TO_MEMBER(params, processId),
        TO_MEMBER(params, rootPath),
        TO_MEMBER(params, rootUri),
        TO_MEMBER(params, initializationOptions),
        TO_MEMBER(params, capabilities),
        TO_MEMBER(params, trace)
    };
}

void fromJSON(JSON &&json, TextDocumentSyncKind &kind) {
    kind = static_cast<TextDocumentSyncKind>(json.asLong());
}

JSON toJSON(TextDocumentSyncKind kind) {
    return JSON(static_cast<int>(kind));
}

void fromJSON(JSON &&json, CompletionOptions &options) {
    FROM_JSON(json, options, resolveProvider);
    FROM_JSON(json, options, triggerCharacters);
}
JSON toJSON(const CompletionOptions &options) {
    return {
        TO_MEMBER(options, resolveProvider),
        TO_MEMBER(options, triggerCharacters)
    };
}

void fromJSON(JSON &&json, SignatureHelpOptions &options) {
    FROM_JSON(json, options, triggerCharacters);
}

JSON toJSON(const SignatureHelpOptions &options) {
    return {
        TO_MEMBER(options, triggerCharacters)
    };
}

void fromJSON(JSON &&json, CodeActionKind &kind) {
    const char *table[] = {
#define GEN_STR(e, s) s,
            EACH_CODE_ACTION_KIND(GEN_STR)
#undef GEN_STR
    };

    for(unsigned int i = 0; i < arraySize(table); i++) {
        if(json.asString() == table[i]) {
            kind = static_cast<CodeActionKind>(i);
        }
    }
}

JSON toJSON(const CodeActionKind &kind) {
    const char *table[] = {
#define GEN_STR(e, s) s,
            EACH_CODE_ACTION_KIND(GEN_STR)
#undef GEN_STR
    };
    return JSON(table[static_cast<unsigned int>(kind)]);
}

void fromJSON(JSON &&json, CodeActionOptions &options) {
    FROM_JSON(json, options, codeActionKinds);
}

JSON toJSON(const CodeActionOptions &options) {
    return {
        TO_MEMBER(options, codeActionKinds)
    };
}

void fromJSON(JSON &&json, CodeLensOptions &options) {
    FROM_JSON(json, options, resolveProvider);
}

JSON toJSON(const CodeLensOptions &options) {
    return {
        TO_MEMBER(options, resolveProvider)
    };
}

void fromJSON(JSON &&json, DocumentOnTypeFormattingOptions &options) {
    FROM_JSON(json, options, firstTriggerCharacter);
    FROM_JSON(json, options, moreTriggerCharacter);
}

JSON toJSON(const DocumentOnTypeFormattingOptions &options) {
    return {
        TO_MEMBER(options, firstTriggerCharacter),
        TO_MEMBER(options, moreTriggerCharacter)
    };
}

void fromJSON(JSON &&json, RenameOptions &options) {
    FROM_JSON(json, options, prepareProvider);
}

JSON toJSON(const RenameOptions &options) {
    return {
        TO_MEMBER(options, prepareProvider)
    };
}

void fromJSON(JSON &&json, DocumentLinkOptions &options) {
    FROM_JSON(json, options, resolveProvider);
}

JSON toJSON(const DocumentLinkOptions &options) {
    return {
        TO_MEMBER(options, resolveProvider)
    };
}

void fromJSON(JSON &&json, ExecuteCommandOptions &options) {
    FROM_JSON(json, options, commands);
}

JSON toJSON(const ExecuteCommandOptions &options) {
    return {
        TO_MEMBER(options, commands)
    };
}

void fromJSON(JSON &&json, SaveOptions &options) {
    FROM_JSON(json, options, includeText);
}

JSON toJSON(const SaveOptions &options) {
    return {
        TO_MEMBER(options, includeText)
    };
}

void fromJSON(JSON &&json, TextDocumentSyncOptions &options) {
    FROM_JSON(json, options, openClose);
    FROM_JSON(json, options, change);
    FROM_JSON(json, options, willSave);
    FROM_JSON(json, options, willSaveWaitUntil);
    FROM_JSON(json, options, save);
}

JSON toJSON(const TextDocumentSyncOptions &options) {
    return {
        TO_MEMBER(options, openClose),
        TO_MEMBER(options, change),
        TO_MEMBER(options, willSave),
        TO_MEMBER(options, willSaveWaitUntil),
        TO_MEMBER(options, save)
    };
}

void fromJSON(JSON &&json, StaticRegistrationOptions &options) {
    FROM_JSON(json, options, id);
}

JSON toJSON(const StaticRegistrationOptions &options) {
    return {
        TO_MEMBER(options, id)
    };
}

void fromJSON(JSON &&json, ServerCapabilities &cap) {
    FROM_JSON(json, cap, textDocumentSync);
    FROM_JSON(json, cap, hoverProvider);
    FROM_JSON(json, cap, completionProvider);
    FROM_JSON(json, cap, signatureHelpProvider);
    FROM_JSON(json, cap, definitionProvider);
    FROM_JSON(json, cap, referencesProvider);
    FROM_JSON(json, cap, documentHighlightProvider);
    FROM_JSON(json, cap, documentSymbolProvider);
    FROM_JSON(json, cap, workspaceSymbolProvider);
    FROM_JSON(json, cap, codeActionProvider);
    FROM_JSON(json, cap, codeLensProvider);
    FROM_JSON(json, cap, documentFormattingProvider);
    FROM_JSON(json, cap, documentRangeFormattingProvider);
    FROM_JSON(json, cap, documentOnTypeFormattingProvider);
    FROM_JSON(json, cap, renameProvider);
    FROM_JSON(json, cap, documentLinkProvider);
    FROM_JSON(json, cap, executeCommandProvider);
}

JSON toJSON(const ServerCapabilities &cap) {
    return {
        TO_MEMBER(cap, textDocumentSync),
        TO_MEMBER(cap, hoverProvider),
        TO_MEMBER(cap, completionProvider),
        TO_MEMBER(cap, signatureHelpProvider),
        TO_MEMBER(cap, definitionProvider),
        TO_MEMBER(cap, referencesProvider),
        TO_MEMBER(cap, documentHighlightProvider),
        TO_MEMBER(cap, documentSymbolProvider),
        TO_MEMBER(cap, workspaceSymbolProvider),
        TO_MEMBER(cap, codeActionProvider),
        TO_MEMBER(cap, codeLensProvider),
        TO_MEMBER(cap, documentFormattingProvider),
        TO_MEMBER(cap, documentRangeFormattingProvider),
        TO_MEMBER(cap, documentOnTypeFormattingProvider),
        TO_MEMBER(cap, renameProvider),
        TO_MEMBER(cap, documentLinkProvider),
        TO_MEMBER(cap, executeCommandProvider)
    };
}

} // namespace rpc
} // namespace ydsh