/*
 * Copyright (C) 2021 Nagisa Sekiguchi
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

#ifndef ARSH_TOOLS_ANALYZER_WORKER_H
#define ARSH_TOOLS_ANALYZER_WORKER_H

#include <functional>
#include <future>
#include <queue>
#include <thread>

namespace arsh::lsp {

class SingleBackgroundWorker {
private:
  std::thread workerThread;
  std::queue<std::function<void()>> tasks;
  std::mutex mutex;
  std::condition_variable condition;
  bool stop{false};

public:
  SingleBackgroundWorker();

  ~SingleBackgroundWorker();

  template <typename Func, typename... Arg>
  std::future<std::invoke_result_t<Func, Arg...>> addTask(Func &&func, Arg &&...arg) {
    using RetType = std::invoke_result_t<Func, Arg...>;
    auto task = ({
      auto bound = std::bind(std::forward<Func>(func), std::forward<Arg>(arg)...);
      std::make_shared<std::packaged_task<RetType()>>(std::move(bound));
    });
    std::future<RetType> future = task->get_future();
    if (!this->addTaskImpl([task] { (*task)(); })) {
      return {};
    }
    return future;
  }

  void addNoreturnTask(std::function<void()> &&task) { this->addTaskImpl(std::move(task)); }

private:
  bool addTaskImpl(std::function<void()> &&task);
};

} // namespace arsh::lsp

#endif // ARSH_TOOLS_ANALYZER_WORKER_H
