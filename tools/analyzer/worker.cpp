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

#include "worker.h"

namespace arsh::lsp {

// ####################################
// ##     SingleBackgroundWorker     ##
// ####################################

SingleBackgroundWorker::SingleBackgroundWorker() {
  this->workerThread = std::thread([&] {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(this->mutex);
        this->condition.wait(lock, [&] { return this->stop || !this->tasks.empty(); });
        if (this->stop && this->tasks.empty()) {
          return;
        }
        task = std::move(this->tasks.front());
        this->tasks.pop();
      }
      task();
    }
  });
}

SingleBackgroundWorker::~SingleBackgroundWorker() {
  {
    std::unique_lock<std::mutex> lock(this->mutex);
    this->stop = true;
  }
  this->condition.notify_all();
  this->workerThread.join();
}

bool SingleBackgroundWorker::addTaskImpl(std::function<void()> &&task) {
  {
    std::unique_lock<std::mutex> lock(this->mutex);
    if (this->stop) {
      return false;
    }
    tasks.push(std::move(task));
  }
  this->condition.notify_one();
  return true;
}

} // namespace arsh::lsp