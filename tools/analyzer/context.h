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

#ifndef ARSH_TOOLS_ANALYZER_CONTEXT_H
#define ARSH_TOOLS_ANALYZER_CONTEXT_H

#include <atomic>

#include "../json/json.h"

#include <cancel.h>
#include <misc/ring_buffer.hpp>

namespace arsh::lsp {

using namespace json;

class CancelPoint : public CancelToken {
private:
  std::atomic<bool> value{false};

public:
  void cancel() { this->value.store(true); }

  bool isCanceled() const { return this->value.load(); }

  bool operator()() override { return this->isCanceled(); }
};

class Context {
private:
  CancelPoint cancel;
  JSON id; // number|string

public:
  explicit Context(JSON &&id) : id(std::move(id)) {}

  const auto &getId() const { return this->id; }

  ObserverPtr<CancelPoint> getCancelPoint() { return makeObserver(this->cancel); }
};

class ContextManager {
private:
  RingBuffer<std::shared_ptr<Context>> ring{15};

public:
  std::shared_ptr<Context> addNew(JSON id);

  void cancel(const JSON &id) const;
};

/**
 *
 * @param id
 * must be 'int|string'
 * @return
 */
std::string toStringCancelId(const JSON &id);

JSON cancelIdToJSON(const Union<int, std::string> &id);

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_CONTEXT_H
