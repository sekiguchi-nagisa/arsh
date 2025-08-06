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

#include "line_buffer.h"
#include "misc/grapheme.hpp"
#include "misc/inlined_stack.hpp"
#include "misc/word.hpp"

namespace arsh {

// ########################
// ##     LineBuffer     ##
// ########################

void LineBuffer::syncNewlinePosList() {
  this->newlinePosList.clear();
  auto ref = this->get();
  for (StringRef::size_type pos = 0;;) {
    auto retPos = ref.find('\n', pos);
    if (retPos != StringRef::npos) {
      this->newlinePosList.push_back(retPos);
      pos = retPos + 1;
    } else {
      break;
    }
  }
}

size_t LineBuffer::prevCharBytes() const {
  auto ref = this->getToCursor();
  size_t byteSize = 0;
  iterateGrapheme(
      ref, [&byteSize](const GraphemeCluster &grapheme) { byteSize = grapheme.getRef().size(); });
  return byteSize;
}

size_t LineBuffer::nextCharBytes() const {
  auto ref = this->getFromCursor();
  size_t byteSize = 0;
  iterateGraphemeUntil(ref, 1, [&byteSize](const GraphemeCluster &grapheme) {
    byteSize = grapheme.getRef().size();
  });
  return byteSize;
}

size_t LineBuffer::prevWordBytes() const {
  auto ref = this->getToCursor();
  size_t byteSize = 0;
  iterateWord(ref, [&byteSize](StringRef word) { byteSize = word.size(); });
  return byteSize;
}

size_t LineBuffer::nextWordBytes() const {
  auto ref = this->getFromCursor();
  size_t byteSize = 0;
  iterateWordUntil(ref, 1, [&byteSize](StringRef word) { byteSize = word.size(); });
  return byteSize;
}

LineBuffer::Interval LineBuffer::findCurLineInterval(bool wholeLine) const {
  unsigned int pos;
  unsigned int len;
  if (this->isSingleLine()) { // single-line
    pos = 0;
    len = (wholeLine ? this->getUsedSize() : this->getCursor());
  } else { // multi-line
    unsigned int index = this->findCurNewlineIndex();
    if (index == 0) {
      pos = 0;
      len = wholeLine ? this->newlinePosList[index] : this->getCursor();
    } else if (index < this->newlinePosList.size()) {
      pos = this->newlinePosList[index - 1] + 1;
      len = (wholeLine ? this->newlinePosList[index] : this->getCursor()) - pos;
    } else {
      pos = this->newlinePosList[index - 1] + 1;
      len = (wholeLine ? this->getUsedSize() : this->getCursor()) - pos;
    }
  }
  return {pos, len};
}

unsigned int LineBuffer::findCurNewlineIndex() const {
  auto iter =
      std::lower_bound(this->newlinePosList.begin(), this->newlinePosList.end(), this->getCursor());
  if (iter == this->newlinePosList.end()) {
    return this->newlinePosList.size();
  }
  return iter - this->newlinePosList.begin();
}

size_t LineBuffer::resolveInsertingSuffix(StringRef &inserting, const bool single) const {
  const StringRef prefix = inserting;
  inserting = "";

  // compute suffix
  bool matched = false;
  size_t offset = this->cursor - std::min<size_t>(this->cursor, prefix.size());
  for (; offset < this->cursor; offset++) {
    const auto suffix = this->get().substr(offset, this->cursor - offset);
    if (prefix.startsWith(suffix)) {
      matched = true;
      break;
    }
  }

  if (matched) {
    size_t insertingSize = prefix.size() - (this->cursor - offset);
    inserting = {prefix.data() + (prefix.size() - insertingSize), insertingSize};
  } else if (single) {
    inserting = prefix; // if candidate does not match previous token, use common candidate prefix
  }
  return matched ? offset : this->cursor;
}

bool LineBuffer::insertToCursor(const StringRef ref, const EditOp editOp) {
  assert(this->cursor <= this->usedSize);
  if (this->usedSize + ref.size() <= this->bufSize) {
    if (this->usedSize == this->cursor) { // insert to last
      memcpy(&this->buf[this->cursor], ref.data(), ref.size());
      this->cursor += ref.size();
      this->usedSize += ref.size();
      this->buf[this->usedSize] = '\0';
    } else {
      memmove(this->buf + this->cursor + ref.size(), this->buf + this->cursor,
              this->usedSize - this->cursor);
      memcpy(&this->buf[this->cursor], ref.data(), ref.size());
      this->cursor += ref.size();
      this->usedSize += ref.size();
      this->buf[this->usedSize] = '\0';
    }
    if (editOp.trackChange) {
      this->trackChange(ChangeOp::INSERT, ref.toString(), editOp.mergeChange);
    }
    return true;
  }
  errno = ENOMEM;
  return false;
}

bool LineBuffer::deleteToCursor(size_t size, std::string *capture, const EditOp editOp) {
  if (this->cursor > 0 && this->usedSize > 0 && size > 0 && size <= this->cursor) {
    std::string delta;
    if (editOp.trackChange || capture) {
      delta = std::string(this->buf + this->cursor - size, size);
    }
    if (capture) {
      *capture = delta;
    }
    memmove(this->buf + this->cursor - size, this->buf + this->cursor,
            this->usedSize - this->cursor);
    this->cursor -= size;
    this->usedSize -= size;
    this->buf[this->usedSize] = '\0';
    if (editOp.trackChange) {
      this->trackChange(ChangeOp::DELETE_TO, std::move(delta), editOp.mergeChange);
    }
    return true;
  }
  return false;
}

bool LineBuffer::deleteFromCursor(size_t size, std::string *capture, const EditOp editOp) {
  if (this->usedSize > 0 && this->cursor < this->usedSize && size > 0 &&
      size <= this->usedSize - this->cursor) {
    std::string delta;
    if (editOp.trackChange || capture) {
      delta = std::string(this->buf + this->cursor, size);
    }
    if (capture) {
      *capture = delta;
    }
    memmove(this->buf + this->cursor, this->buf + this->cursor + size,
            this->usedSize - this->cursor - size);
    this->usedSize -= size;
    this->buf[this->usedSize] = '\0';
    if (editOp.trackChange) {
      this->trackChange(ChangeOp::DELETE_FROM, std::move(delta), editOp.mergeChange);
    }
    return true;
  }
  return false;
}

bool LineBuffer::moveCursorUpDown(bool up) {
  const auto oldCursor = this->getCursor();
  if (up) { // up
    this->moveCursorToStartOfLine();
    if (this->getCursor() == 0) {
      this->cursor = oldCursor;
      return false;
    }
    this->cursor--;
  } else { // down
    this->moveCursorToEndOfLine();
    if (this->getCursor() == this->getUsedSize()) {
      this->cursor = oldCursor;
      return false;
    }
    this->cursor++;
  }
  StringRef dest = this->getCurLine(true);
  this->cursor = oldCursor;

  // resolve line to current position
  size_t count = iterateGrapheme(this->getCurLine(false), [](const GraphemeCluster &) {});
  GraphemeCluster ret;
  size_t retCount =
      iterateGraphemeUntil(dest, count, [&ret](const GraphemeCluster &scanned) { ret = scanned; });
  if (retCount) {
    this->cursor = ret.getRef().end() - this->buf;
  } else {
    this->cursor = dest.begin() - this->buf;
  }
  return true;
}

bool LineBuffer::undo() {
  if (this->changeIndex == 0) {
    return false;
  }
  this->changeIndex--;
  this->changes[this->changeIndex].merge = false;
  const auto *change = &this->changes[this->changeIndex];
  do {
    constexpr EditOp editOp{.trackChange = false, .mergeChange = false};
    this->cursor = change->cursor;
    switch (change->type) {
    case ChangeOp::INSERT:
      this->deleteToCursor(change->delta.size(), nullptr, editOp);
      break;
    case ChangeOp::DELETE_TO:
    case ChangeOp::DELETE_FROM:
      this->insertToCursor(change->delta, editOp);
      if (change->type == ChangeOp::DELETE_FROM) {
        this->cursor = change->cursor;
      }
      break;
    }
    change = change->prev.get();
  } while (change);
  return true;
}

bool LineBuffer::redo() {
  if (this->changeIndex == this->changes.size()) {
    return false;
  }
  assert(this->changeIndex < this->changes.size());
  this->changes[this->changeIndex].merge = false;

  // reverse order of changes
  InlinedStack<const Change *, 4> stack;
  for (const auto *change = &this->changes[this->changeIndex]; change;
       change = change->prev.get()) {
    stack.push(change);
  }

  // revert changes
  this->changeIndex++;
  do {
    constexpr EditOp editOp{.trackChange = false, .mergeChange = false};
    switch (const auto *change = stack.back(); change->type) {
    case ChangeOp::INSERT:
      this->cursor = change->cursor - change->delta.size();
      this->insertToCursor(change->delta, editOp);
      break;
    case ChangeOp::DELETE_TO:
      this->cursor = change->cursor + change->delta.size();
      this->deleteToCursor(change->delta.size(), nullptr, editOp);
      break;
    case ChangeOp::DELETE_FROM:
      this->cursor = change->cursor;
      this->deleteFromCursor(change->delta.size(), nullptr, editOp);
      break;
    }
    stack.pop();
  } while (stack.size());
  return true;
}

bool LineBuffer::Change::tryMerge(const Change &o) {
  if (!this->merge || !o.merge) {
    return false;
  }
  if (this->type != o.type) {
    return false;
  }
  switch (this->type) {
  case ChangeOp::INSERT:
    if (this->cursor == o.cursor - o.delta.size()) {
      this->cursor = o.cursor;
      this->delta += o.delta;
      return true;
    }
    break;
  case ChangeOp::DELETE_TO:
    if (this->cursor == o.cursor + o.delta.size()) {
      this->cursor = o.cursor;
      this->delta.insert(0, o.delta);
      return true;
    }
    break;
  case ChangeOp::DELETE_FROM:
    if (this->cursor == o.cursor) {
      this->delta += o.delta;
      return true;
    }
    break;
  }
  return false;
}

void LineBuffer::trackChange(ChangeOp op, std::string &&delta, bool merge) {
  while (this->changeIndex < this->changes.size()) {
    this->changes.pop_back();
  }

  Change newChange(op, this->cursor, std::move(delta), merge);
  if (!this->changes.empty()) {
    bool r = this->changes.back().tryMerge(newChange);
    this->changes.back().merge = r;
    if (r) {
      return;
    }
    if (this->atomicEdit) {
      newChange.prev = std::make_unique<const Change>(std::move(this->changes.back()));
      this->changes.pop_back();
    }
  }
  this->changes.push_back(std::move(newChange));
  this->changeIndex = this->changes.size();
}

// ######################
// ##     KillRing     ##
// ######################

void KillRing::expand(unsigned int afterCap) {
  if (this->buf.allocSize() == RingBuffer<std::string>::alignAllocSize(afterCap)) {
    return; // do nothing
  }

  RingBuffer<std::string> newBuf(afterCap);
  while (!this->buf.empty()) {
    newBuf.push_back(std::move(this->buf.front()));
    this->buf.pop_front();
  }
  this->buf = std::move(newBuf);
  if (*this) {
    this->reset();
  }
}

} // namespace arsh