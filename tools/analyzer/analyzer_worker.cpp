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

AnalyzerWorker::AnalyzerWorker(std::reference_wrapper<LoggerBase> logger,
                               DiagnosticCallback &&callback, bool diagSupportVersion,
                               const std::string &testDir, uint64_t seed,
                               std::chrono::milliseconds debounceTime)
    : logger(logger), diagSupportVersion(diagSupportVersion), debounceTime(debounceTime),
      diagnosticCallback(std::move(callback)), state(State::create(testDir, seed)) {
  this->workerThread = std::thread([this] {
    while (!this->stop) {
      std::unique_ptr<Task> task;
      for (unsigned int i = 0;; i++) {
        READER_LOCK(lock);
        auto time = this->debounceTime + std::chrono::milliseconds(1 << i);
        const bool r = this->requestCond.wait_for(lock, time, [this] {
          return this->stop ||
                 (this->status == Status::PENDING && !this->state.modifiedSrcIds.empty());
        });
        if (r) {
          if (this->stop) {
            return;
          }
          if (const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                  getCurrentTimestamp() - this->lastRequestTimestamp);
              elapsed < this->debounceTime &&
              this->state.modifiedSrcIds.size() < MAX_PENDING_CHANGED_SOURCES) {
            i = 0;
            continue;
          }
          break;
        }
      }
      {
        // prepare rebuild
        WRITER_LOCK(lock);
        this->status = Status::RUNNING;
        auto newState = this->state.deepCopy();
        this->state.modifiedSrcIds.clear();
        DiagnosticEmitter emitter(std::shared_ptr(newState.srcMan), this->diagnosticCallback,
                                  this->diagSupportVersion);
        task = std::make_unique<Task>(this->logger, this->sysConfig, std::move(newState),
                                      std::move(emitter), std::make_shared<CancelPoint>());
        LOG(LogLevel::INFO, "prepare rebuild for %d sources",
            static_cast<unsigned int>(task->state.modifiedSrcIds.size()));
      }

      decltype(this->finishedCallbacks) tmpCallbacks;
      while (!this->stop) {
        // do rebuild
        task->run();

        // merge result
        WRITER_LOCK(lock);
        task->state.mergeSources(this->state);
        this->state.~State();
        new (&this->state) State(task->state);
        this->state.modifiedSrcIds.clear();
        if (!task->state.modifiedSrcIds.empty()) {
          this->status = Status::RUNNING;
        } else {
          this->status = Status::FINISHED;
          tmpCallbacks.swap(this->finishedCallbacks);
          this->finishCond.notify_all();
          LOG(LogLevel::INFO, "rebuild all finished");

          // try close source
          for (auto &srcId : this->closingSrcIds) {
            if (const auto removed = this->state.remove(srcId)) {
              LOG(LogLevel::INFO, "close pending textDocument: %s", removed->getPath().c_str());
            }
          }
          this->closingSrcIds.clear();
          break;
        }
      }

      // kick callback
      if (!this->stop) {
        const unsigned int size = tmpCallbacks.size();
        for (unsigned int i = 0; i < size; i++) {
          LOG(LogLevel::INFO, "kick pending callback: %d of %d", i + 1, size);
          tmpCallbacks[i](task->state);
        }
      }
    }
  });
}

AnalyzerWorker::~AnalyzerWorker() {
  this->stop = true;
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
      return;
    }
  }
  LOG(LogLevel::ERROR, "broken uri: %s", params.textDocument.uri.c_str());
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

void AnalyzerWorker::requestSourceClose(const DidCloseTextDocumentParams &params) {
  WRITER_LOCK(lock);
  if (auto resolved = resolveSource(this->logger, *this->state.srcMan, params.textDocument)) {
    if (this->status == Status::FINISHED) {
      if (const auto removed = this->state.remove(resolved.asOk()->getSrcId())) {
        LOG(LogLevel::INFO, "immediately close textDocument: %s", removed->getPath().c_str());
      }
    } else {
      this->closingSrcIds.emplace(resolved.asOk()->getSrcId());
    }
  }
}

void AnalyzerWorker::requestSourceUpdateUnsafe(const std::string &path, int newVersion,
                                               std::string &&newContent) {
  if (auto [src, s] = this->state.updateSource(path, newVersion, std::move(newContent)); src) {
    this->closingSrcIds.erase(src->getSrcId()); // clear pending close requests
    if (s) {
      this->lastRequestTimestamp = getCurrentTimestamp();
      if (this->status == Status::FINISHED) {
        this->status = Status::PENDING;
      }
      this->requestCond.notify_all();
    } else {
      LOG(LogLevel::INFO, "not update source due to no modification: %s", path.c_str());
    }
  } else {
    LOG(LogLevel::ERROR, "reach opened file limit");
  }
}

void AnalyzerWorker::requestForceRebuild() {
  WRITER_LOCK(lock);
  LOG(LogLevel::INFO, "at requestForceRebuild: %s", toString(this->status));
  if (this->status == Status::PENDING) {
    this->lastRequestTimestamp = timestamp{};
    this->requestCond.notify_all();
    LOG(LogLevel::INFO, "try force rebuild for pending requests: %d",
        static_cast<unsigned int>(this->state.modifiedSrcIds.size()));
  }
}

void AnalyzerWorker::asyncStateWith(std::function<void(const State &)> &&callback) {
  assert(callback);
  {
    READER_LOCK(lock);
    if (this->status == Status::FINISHED) { // kick callback
      LOG(LogLevel::INFO, "immediately kick callback");
      callback(this->state);
      return;
    }
  }

  {
    WRITER_LOCK(lock);
    if (this->status != Status::FINISHED &&
        this->finishedCallbacks.size() < MAX_PENDING_CALLBACKS) {
      LOG(LogLevel::INFO, "put callback due to: %s", toString(this->status));
      this->finishedCallbacks.push_back(std::move(callback));
      return;
    }
  }

  LOG(LogLevel::INFO, "number of pending callback reaches limit");
  this->waitStateWith(std::move(callback));
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

const char *toString(AnalyzerWorker::Status s) {
  switch (s) {
  case AnalyzerWorker::Status::FINISHED:
    return "finished";
  case AnalyzerWorker::Status::PENDING:
    return "pending";
  case AnalyzerWorker::Status::RUNNING:
    return "running";
  }
  return "";
}

void AnalyzerWorker::State::mergeSources(const State &other) {
  if (this != &other) {
    for (auto &id : other.modifiedSrcIds) {
      auto src = other.srcMan->findById(id);
      src = this->srcMan->add(src);
      assert(src);
      this->modifiedSrcIds.emplace(src->getSrcId());
    }
  }
}

std::pair<SourcePtr, bool> AnalyzerWorker::State::updateSource(StringRef path, int newVersion,
                                                               std::string &&newContent) {
  SourcePtr prevOpened = this->srcMan->find(path);
  if (auto src = this->srcMan->update(path, newVersion, std::move(newContent))) {
    if (prevOpened && prevOpened->equalsDigest(*src)) {
      return {std::move(src), false}; // source modification does not affect rebuild decision
    }
    this->modifiedSrcIds.emplace(src->getSrcId());
    return {std::move(src), true};
  }
  return {nullptr, false};
}

SourcePtr AnalyzerWorker::State::remove(ModId targetId) {
  if (this->archives.removeIfUnused(targetId)) {
    this->indexes.remove(targetId);
    return this->srcMan->remove(targetId);
  }
  return nullptr;
}

void AnalyzerWorker::Task::run() {
  LOG(LogLevel::INFO, "rebuild started for %d sources",
      static_cast<unsigned int>(this->state.modifiedSrcIds.size()));

  // prepare
  AnalyzerAction action;
  SymbolIndexer indexer(this->sysConfig, this->state.indexes);
  indexer.setLogger(makeObserver(this->logger.get()));
  ExtraChecker extraChecker(this->emitter);
  MultipleNodePass passes;
  passes.add(makeObserver(extraChecker));
  passes.add(makeObserver(indexer));
  action.emitter.reset(&this->emitter);
  action.pass = makeObserver(passes);

  Analyzer analyzer(this->sysConfig, *this->state.srcMan, this->state.archives,
                    makeObserver(*this->cancelPoint), makeObserver(this->logger.get()));

  // rebuild
  if (this->state.modifiedSrcIds.size() == 1) { // fast path
    const ModId modId = *this->state.modifiedSrcIds.begin();
    const auto oldArchive = this->state.archives.find(modId);
    this->state.modifiedSrcIds.clear();
    const auto ret = this->doAnalyze(analyzer, modId, action, "modified");
    if (ret) {
      if (!oldArchive || ret->equalsDigest(*oldArchive)) {
        LOG(LogLevel::INFO, "digest of archive: id=%d has not changed, rebuild finished",
            toUnderlying(modId));
        return;
      }
    }
    this->state.archives.revert({modId});
    if (ret) {
      this->state.archives.add(ret);
    }
    LOG(LogLevel::INFO, "digest of archive: id=%d has changed, fallback to slow path",
        toUnderlying(modId));
  } else {
    this->state.archives.revert(std::unordered_set(this->state.modifiedSrcIds));
  }
  for (auto iter = this->state.modifiedSrcIds.begin(); iter != this->state.modifiedSrcIds.end();) {
    if (!this->state.archives.find(*iter)) { // analyze non-rebuilt source
      if (!this->doAnalyze(analyzer, *iter, action, "modified")) {
        break;
      }
    }
    iter = this->state.modifiedSrcIds.erase(iter);
  }

  while (!this->cancelPoint->isCanceled()) {
    auto targetId = this->state.archives.getFirstRevertedModId();
    if (!targetId.hasValue()) {
      break;
    }
    if (!this->doAnalyze(analyzer, targetId.unwrap(), action, "reverted")) {
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

ModuleArchivePtr AnalyzerWorker::Task::doAnalyze(Analyzer &analyzer, ModId modId,
                                                 AnalyzerAction &action,
                                                 const char *message) const {
  auto src = this->state.srcMan->findById(modId);
  assert(src);
  LOG(LogLevel::INFO, "analyze %s src: id=%d, version=%d, path=%s", message,
      toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
  auto r = analyzer.analyze(src, action);
  LOG(LogLevel::INFO, "analyze %s: id=%d, version=%d, path=%s", r ? "finished" : "canceled",
      toUnderlying(src->getSrcId()), src->getVersion(), src->getPath().c_str());
  return r;
}

} // namespace arsh::lsp