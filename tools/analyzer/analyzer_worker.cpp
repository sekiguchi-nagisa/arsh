/*
 * Copyright (C) 2025 Nagisa Sekiguchi
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

#include <format_util.h>

#include "analyzer.h"
#include "analyzer_worker.h"
#include "extra_checker.h"
#include "indexer.h"

namespace arsh::lsp {

// ############################
// ##     AnalyzerWorker     ##
// ############################

#define LLOG(logger, L, ...)                                                                       \
  do {                                                                                             \
    (logger).enabled(L) && (logger)(L, __VA_ARGS__);                                               \
  } while (false)

#define LOG(L, ...) LLOG(this->logger.get(), L, __VA_ARGS__)

#define WRITER_LOCK(LOCK) std::unique_lock<std::shared_mutex> LOCK(this->mutex)

#define READER_LOCK(LOCK) std::shared_lock<std::shared_mutex> LOCK(this->mutex)

static bool shouldRebuild(const AnalyzerWorker::Status s, const std::unordered_set<ModId> &srcIds,
                          const timestamp reqTimestamp,
                          const std::chrono::milliseconds debounceTime) {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::minutes>(getCurrentTimestamp() - reqTimestamp);
  return s == AnalyzerWorker::Status::PENDING && !srcIds.empty() && elapsed >= debounceTime;
}

AnalyzerWorker::AnalyzerWorker(std::reference_wrapper<LoggerBase> logger,
                               DiagnosticCallback &&callback, bool diagSupportVersion,
                               std::chrono::milliseconds debounceTime)
    : logger(logger), diagSupportVersion(diagSupportVersion), debounceTime(debounceTime),
      diagnosticCallback(std::move(callback)) {
  this->workerThread = std::thread([&] {
    while (true) {
      std::unique_ptr<Task> task;
      {
        WRITER_LOCK(lock);
        this->requestCond.wait(lock, [&] {
          return this->status == Status::DISPOSED ||
                 shouldRebuild(this->status, this->state.modifiedSrcIds, this->lastRequestTimestamp,
                               this->debounceTime);
        });
        if (this->status == Status::DISPOSED) {
          return;
        }

        // prepare rebuild
        this->status = Status::RUNNING;
        auto newState = this->state.deepCopy();
        this->state.modifiedSrcIds.clear();
        DiagnosticEmitter emitter(std::shared_ptr(newState.srcMan),
                                  DiagnosticCallback(this->diagnosticCallback),
                                  this->diagSupportVersion);
        task = std::make_unique<Task>(this->logger, this->sysConfig, std::move(newState),
                                      std::move(emitter), std::make_shared<CancelPoint>());
      }

      do {
        // do rebuild
        task->run();

        // merge result
        WRITER_LOCK(lock);
        task->state.mergeSources(this->state);
        this->state = task->state;
        this->state.modifiedSrcIds.clear();
        if (task->state.modifiedSrcIds.empty()) {
          this->status = Status::FINISHED;
          this->finishCond.notify_all();
          task = nullptr;
        } else {
          this->status = Status::RUNNING;
        }
      } while (task);
    }
  });
}

AnalyzerWorker::~AnalyzerWorker() {
  {
    WRITER_LOCK(lock);
    this->status = Status::DISPOSED;
  }
  this->finishCond.notify_all();
  this->requestCond.notify_all();
  this->workerThread.join();
}

void AnalyzerWorker::requestSourceOpen(const DidOpenTextDocumentParams &params) {
  WRITER_LOCK(lock);

  if (auto uri = uri::URI::parse(params.textDocument.uri)) {
    if (auto fullPath = this->state.srcMan->resolveURI(uri); !fullPath.empty()) {
      this->requestSourceUpdateUnsafe(fullPath, params.textDocument.version,
                                      std::string(params.textDocument.text));
    }
  }
}

void AnalyzerWorker::requestSourceChange(const DidChangeTextDocumentParams &params) {
  WRITER_LOCK(lock);

  auto resolved = resolveSource(this->logger, *this->state.srcMan, params.textDocument);
  if (!resolved) {
    return;
  }
  auto src = std::move(resolved).take();
  std::string content = src->getContent();
  for (auto &change : params.contentChanges) {
    if (!applyChange(content, change)) {
      LOG(LogLevel::ERROR, "textDocument may lack consistency: %s", src->getPath().c_str());
      return;
    }
  }
  this->requestSourceUpdateUnsafe(src->getPath(), params.textDocument.version, std::move(content));
}

void AnalyzerWorker::requestSourceUpdateUnsafe(StringRef path, int newVersion,
                                               std::string &&newContent) {
  if (auto src = this->state.srcMan->update(path, newVersion, std::move(newContent))) {
    this->state.modifiedSrcIds.emplace(src->getSrcId());
    this->lastRequestTimestamp = getCurrentTimestamp();
    if (this->status == Status::FINISHED) {
      this->status = Status::PENDING;
    }
    this->requestCond.notify_all();
  } else {
    LOG(LogLevel::ERROR, "reach opened file limit");
  }
}

void AnalyzerWorker::waitForAnalyzerFinished() {
  WRITER_LOCK(lock);
  this->finishCond.wait(lock, [&] { return this->status == Status::FINISHED; });
}

Result<SourcePtr, std::string> resolveSource(LoggerBase &logger, const SourceManager &srcMan,
                                             const TextDocumentIdentifier &doc) {
  auto uri = uri::URI::parse(doc.uri);
  if (!uri) {
    std::string err;
    formatTo(err, "broken uri: %s", doc.uri.c_str());
    LLOG(logger, LogLevel::ERROR, "%s", err.c_str());
    return Err(std::move(err));
  }
  auto fullPath = srcMan.resolveURI(uri);
  auto src = srcMan.find(fullPath);
  if (!src) {
    std::string str;
    formatTo(str, "broken textDocument: %s", doc.uri.c_str());
    LLOG(logger, LogLevel::ERROR, "%s", str.c_str());
    return Err(std::move(str));
  }
  return Ok(std::move(src));
}

static Optional<SymbolRequest> toRequest(const Source &src, Position position) {
  auto pos = toTokenPos(src.getContent(), position);
  if (!pos.hasValue()) {
    return {};
  }
  return SymbolRequest{.modId = src.getSrcId(), .pos = pos.unwrap()};
}

Result<std::pair<SourcePtr, SymbolRequest>, std::string>
resolvePosition(LoggerBase &logger, const SourceManager &srcMan,
                const TextDocumentPositionParams &params) {
  auto resolved = resolveSource(logger, srcMan, params.textDocument);
  if (!resolved) {
    return Err(std::move(resolved).takeError());
  }
  auto src = std::move(resolved).take();
  assert(src);
  auto req = toRequest(*src, params.position);
  if (!req.hasValue()) {
    std::string err;
    formatTo(err, "broken position at: %s:%s", params.textDocument.uri.c_str(),
             params.position.toString().c_str());
    LLOG(logger, LogLevel::ERROR, "%s", err.c_str());
    return Err(std::move(err));
  }
  return Ok(std::make_pair(std::move(src), req.unwrap()));
}

void AnalyzerWorker::State::mergeSources(const State &other) {
  for (auto &id : other.modifiedSrcIds) {
    auto src = other.srcMan->findById(id);
    src = this->srcMan->add(src);
    assert(src);
    this->modifiedSrcIds.emplace(src->getSrcId());
  }
}

void AnalyzerWorker::Task::run() {
  LOG(LogLevel::INFO, "rebuild started");

  // prepare
  {
    auto tmp(this->state.modifiedSrcIds);
    this->state.archives.revert(std::move(tmp));
  }

  AnalyzerAction action;
  SymbolIndexer indexer(this->sysConfig, this->state.indexes);
  indexer.setLogger(makeObserver(this->logger.get()));
  ExtraChecker extraChecker(this->emitter);
  MultipleNodePass passes;
  passes.add(makeObserver(extraChecker));
  passes.add(makeObserver(indexer));
  action.emitter.reset(&this->emitter);
  action.pass = makeObserver(passes);

  // rebuild
  Analyzer analyzer(this->sysConfig, *this->state.srcMan, this->state.archives,
                    makeObserver(*this->cancelPoint), makeObserver(this->logger.get()));
  for (auto &e : this->state.modifiedSrcIds) {
    if (this->state.archives.find(e)) {
      continue;
    }
    auto src = this->state.srcMan->findById(e);
    assert(src);
    LOG(LogLevel::INFO, "analyze modified src: id=%d, version=%d, path=%s",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    auto r = analyzer.analyze(src, action);
    LOG(LogLevel::INFO, "analyze %s: id=%d, version=%d, path=%s", r ? "finished" : "canceled",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    if (!r) {
      break;
    }
  }

  while (!this->cancelPoint->isCanceled()) {
    auto targetId = this->state.archives.getFirstRevertedModId();
    if (!targetId.hasValue()) {
      break;
    }
    auto src = this->state.srcMan->findById(targetId.unwrap());
    assert(src);
    LOG(LogLevel::INFO, "analyze revered src: id=%d, version=%d, path=%s",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    auto r = analyzer.analyze(src, action);
    LOG(LogLevel::INFO, "analyze %s: id=%d, version=%d, path=%s", r ? "finished" : "canceled",
        toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
    if (!r) {
      break;
    }
  }
  if (this->cancelPoint->isCanceled()) {
    LOG(LogLevel::INFO, "rebuild canceled");
  } else {
    this->state.modifiedSrcIds.clear();
    LOG(LogLevel::INFO, "rebuild finished");
  }
}

} // namespace arsh::lsp