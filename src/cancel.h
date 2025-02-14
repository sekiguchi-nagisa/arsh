/*
 * Copyright (C) 2023 Nagisa Sekiguchi
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

#ifndef ARSH_CANCEL_H
#define ARSH_CANCEL_H

struct CancelToken {
  virtual ~CancelToken() = default;

  /**
   * cancellation for type-check, expansion
   * @return
   * if canceled, return true
   */
  virtual bool isCanceled() const { return false; }
};

#endif // ARSH_CANCEL_H
