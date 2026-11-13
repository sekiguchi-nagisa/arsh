/*
 * Copyright (C) 2026 Nagisa Sekiguchi
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

/**
 * Node.js side of the regex differential test (see difftest.cpp).
 *
 * It is the oracle: it compiles each pattern with the engine used by Node.js (V8) and reports
 * the first match of `RegExp.prototype.exec`, so the arsh regex engine can be compared against
 * the reference implementation of the ECMAScript RegExp semantics.
 *
 * Protocol: one JSON object per line in both directions.
 *
 *   request:  {"id": <int>, "pattern": <string>, "flags": <string>, "input": <string>}
 *   response: {"id": <int>, "status": "match", "captures": [[start, end], null, ...]}
 *             {"id": <int>, "status": "nomatch"}
 *             {"id": <int>, "status": "syntax", "message": <string>}
 *             {"id": <int>, "status": "error", "message": <string>}
 *
 * Capture offsets are UTF-16 code unit indices (what JavaScript reports) and `null` marks an
 * unmatched group. The `d` (hasIndices) flag is always added so the indices are available.
 *
 * Before the first response a single banner line is written:
 *
 *   {"status": "hello", "node": ..., "unicode": ..., "capabilities": {...}}
 *
 * `unicode` is the Unicode version bundled with the Node.js executable; difftest.cpp uses it to
 * avoid comparing behavior that depends on the Unicode data version. `capabilities` reports the
 * syntax features this Node.js understands, so cases requiring a newer engine are skipped
 * instead of being reported as divergences.
 *
 * The requests are answered as soon as a full line arrives (stdin is read synchronously), and
 * every response is written with fs.writeSync so it is flushed immediately. That keeps the
 * harness free to interleave writes and reads without risking a pipe deadlock.
 */

"use strict";

const fs = require("fs");

function probe(fn) {
  try {
    return fn() === true;
  } catch (e) {
    return false;
  }
}

const capabilities = {
  // the whole harness relies on the capture indices, so a Node.js without the `d` flag cannot
  // be used as the oracle at all (difftest.cpp reports it as a hard error).
  "has-indices": probe(() => new RegExp("(a)", "ud").exec("a").indices[0][0] === 0),
  "unicode-sets": probe(() => new RegExp("[\\p{L}--[a-z]]", "v").test("A")),
  "modifiers": probe(() => new RegExp("(?i:a)", "u").test("A")),
  "property-of-strings": probe(() => new RegExp("\\p{RGI_Emoji}", "v").test("\u{1F600}")),
};

function respond(req) {
  let re;
  try {
    re = new RegExp(req.pattern, req.flags + "d");
  } catch (e) {
    return {id: req.id, status: "syntax", message: String(e.message)};
  }

  let match;
  try {
    match = re.exec(req.input);
  } catch (e) {
    // e.g. look-behind or backreference related runtime errors
    return {id: req.id, status: "error", message: String(e.message)};
  }

  if (match === null) {
    return {id: req.id, status: "nomatch"};
  }

  const captures = [];
  for (let i = 0; i < match.length; i++) {
    const index = match.indices[i];
    captures.push(index === undefined ? null : [index[0], index[1]]);
  }
  return {id: req.id, status: "match", captures: captures};
}

function emit(value) {
  fs.writeSync(1, JSON.stringify(value) + "\n");
}

emit({
  status: "hello",
  node: process.versions.node,
  unicode: process.versions.unicode,
  capabilities: capabilities,
});

// stdin is decoded from a byte buffer, not a string, so a multi-byte UTF-8 sequence split
// across two reads is still decoded correctly.
let pending = Buffer.alloc(0);
const chunk = Buffer.alloc(65536);
for (;;) {
  let size;
  try {
    size = fs.readSync(0, chunk, 0, chunk.length, null);
  } catch (e) {
    if (e.code === "EAGAIN") {
      continue;
    }
    if (e.code === "EOF") {
      break;
    }
    throw e;
  }
  if (size === 0) {
    break;
  }
  pending = Buffer.concat([pending, chunk.subarray(0, size)]);
  let pos;
  while ((pos = pending.indexOf(0x0a)) >= 0) {
    const line = pending.subarray(0, pos).toString("utf8");
    pending = pending.subarray(pos + 1);
    if (line.length > 0) {
      emit(respond(JSON.parse(line)));
    }
  }
}
if (pending.length > 0) {
  const line = pending.toString("utf8");
  if (line.length > 0) {
    emit(respond(JSON.parse(line)));
  }
}
