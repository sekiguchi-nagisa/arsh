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

#ifndef ARSH_TOOLS_ANALYZER_ANALYZER_WORKER_H
#define ARSH_TOOLS_ANALYZER_ANALYZER_WORKER_H

#include <condition_variable>
#include <shared_mutex>
#include <thread>

#include "analyzer.h"
#include "index.h"

namespace arsh::lsp {

class AnalyzerWorker {
public:
  struct State {
    std::shared_ptr<SourceManager> srcMan{std::make_shared<SourceManager>()};
    ModuleArchives archives;
    SymbolIndexes indexes;
    std::unordered_set<ModId> modifiedSrcIds;

    static State create(const std::string &testDir, uint64_t seed) {
      return {.srcMan = std::make_shared<SourceManager>(testDir),
              .archives = ModuleArchives(seed),
              .indexes = {},
              .modifiedSrcIds = {}};
    }

    State deepCopy() const {
      return {.srcMan = this->srcMan->copy(),
              .archives = this->archives,
              .indexes = this->indexes,
              .modifiedSrcIds = this->modifiedSrcIds};
    }

    void mergeSources(const State &other);

    SourcePtr remove(ModId targetId);
  };

  struct Task {
    std::reference_wrapper<LoggerBase> logger;
    std::reference_wrapper<const SysConfig> sysConfig;
    State state;
    DiagnosticEmitter emitter;
    std::shared_ptr<const CancelPoint> cancelPoint;

    Task(const std::reference_wrapper<LoggerBase> logger,
         const std::reference_wrapper<const SysConfig> sysConfig, State &&state,
         DiagnosticEmitter &&emitter, std::shared_ptr<const CancelPoint> &&cancelPoint)
        : logger(logger), sysConfig(sysConfig), state(std::move(state)), emitter(emitter),
          cancelPoint(std::move(cancelPoint)) {}

    void run();
  };

  enum class Status : unsigned char {
    FINISHED, // waiting request (analyze already finished)
    PENDING,  // pending request
    RUNNING,  // analyzer running
  };

private:
  const std::reference_wrapper<LoggerBase> logger;
  const SysConfig sysConfig;
  const bool diagSupportVersion;
  const std::chrono::milliseconds debounceTime;
  const DiagnosticCallback diagnosticCallback;

  std::thread workerThread;
  std::shared_mutex mutex;
  std::condition_variable_any requestCond;
  std::condition_variable_any finishCond;

  // mutable shared states
  std::atomic_bool stop{false};
  Status status{Status::FINISHED};
  timestamp lastRequestTimestamp;
  State state;
  std::vector<std::function<void(const State &)>> finishedCallbacks;
  std::unordered_set<ModId> closingSrcIds;

  static constexpr unsigned int MAX_PENDING_CHANGED_SOURCES = 8;
  static constexpr unsigned int MAX_PENDING_CALLBACKS = 8;

public:
  AnalyzerWorker(std::reference_wrapper<LoggerBase> logger, DiagnosticCallback &&callback,
                 bool diagSupportVersion, const std::string &testDir, uint64_t seed,
                 std::chrono::milliseconds debounceTime);

  ~AnalyzerWorker();

  const SysConfig &getSysConfig() const { return this->sysConfig; }

  void requestSourceOpen(const DidOpenTextDocumentParams &params);

  void requestSourceChange(const DidChangeTextDocumentParams &params);

  void requestSourceClose(const DidCloseTextDocumentParams &params);

  void requestForceRebuild();

  template <typename Reader>
  static constexpr bool reader_requirement_v = std::is_invocable_v<Reader, const State &>;

  template <typename Reader, enable_when<reader_requirement_v<Reader>> = nullptr>
  auto fetchStateWith(Reader &&reader) -> std::invoke_result_t<Reader, const State &> {
    std::shared_lock lock(this->mutex); // reader lock
    if constexpr (std::is_void_v<std::invoke_result_t<Reader, const State &>>) {
      reader(this->state);
      return;
    } else {
      return reader(this->state);
    }
  }

  template <typename Reader, enable_when<reader_requirement_v<Reader>> = nullptr>
  auto waitStateWith(Reader &&reader) -> std::invoke_result_t<Reader, const State &> {
    this->requestForceRebuild();
    std::shared_lock lock(this->mutex); // reader lock
    this->finishCond.wait(lock, [&] { return this->status == Status::FINISHED; });
    if constexpr (std::is_void_v<std::invoke_result_t<Reader, const State &>>) {
      reader(this->state);
      return;
    } else {
      return reader(this->state);
    }
  }

  /**
   *
   * @param callback
   * not null
   */
  void asyncStateWith(std::function<void(const State &)> &&callback);

private:
  void requestSourceUpdateUnsafe(StringRef path, int newVersion, std::string &&newContent);
};

Result<SourcePtr, std::string> resolveSource(LoggerBase &logger, const SourceManager &srcMan,
                                             const TextDocumentIdentifier &doc);

Result<std::pair<SourcePtr, SymbolRequest>, std::string>
resolvePosition(LoggerBase &logger, const SourceManager &srcMan,
                const TextDocumentPositionParams &params);

const char *toString(AnalyzerWorker::Status s);

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_ANALYZER_WORKER_H
