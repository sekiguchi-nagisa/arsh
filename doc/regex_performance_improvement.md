# arsh 正規表現エンジン 改善提案（`result.csv` ベンチマーク分析）

本ドキュメントは、`tools/regex-bench` による18パターンの計測結果（`result.csv`）と、arsh の正規表現エンジン本体（`src/regex/`）の実装分析にもとづく改善提案を、1つの Markdown 文書としてまとめたものです。

---

## 1. ベンチマーク結果の整理

18パターンの合計値（`result.csv` より集計）:

| engine | 合計時間 [ms] | score | mem_inst [B] | mem_run [B] |
|---|---:|---:|---:|---:|
| srell | 345.7 | 88 | 43016 | 12832 |
| quickjs | 10779.7 | 44 | 2512 | 14848 |
| hermes | 11780.3 | 10 | 37496 | 20952 |
| **arsh** | **22832.3** | **18** | **7288** | **21416** |
| boost | 2002968.8 | 56 | 33984 | 11208 |
| cppstd | 3036156.9 | 0 | 53816 | 47688 |

arsh の特性は明快に二極化しています。

- **速い（実用十分）**: `Twain` 3.6ms（最速）、`["'][^"']{0,30}[?!\.]["']` 34.0ms、`\p{Sm}` 79.3ms。いずれもトップレベル先頭が `String` / `CharSet` で、`vm.cpp` の先頭検索 fast path が効いている。インスタンスメモリも合計 7KB で全エンジン中最小。
- **遅い（要対策）**: `Huck[a-zA-Z]+|Saw[a-zA-Z]+` 385ms（SRELL比 193倍）、`Tom|Sawyer|…` 681ms（235倍）、`.{0,2}(Tom|…)` 2002ms（345倍）、`[a-z]shing` 160ms、`\b\w+nn\b` 379ms、`[a-zA-Z]+ing` 753ms、`(.*?,){13}z` 13305ms。**例外なく「先頭が `Alt`／ループ／`IChar`／`\b`／文字クラス」のパターン**。

### パターン別（arsh vs SRELL）

| pattern | arsh | srell | quickjs | hermes | arsh/srell |
|---|---:|---:|---:|---:|---:|
| `Twain` | 3.6 | 9.4 | 106.9 | 65.0 | 0.4x |
| `(?i)Twain` | 185.2 | 9.4 | 133.8 | 86.3 | 19.7x |
| `[a-z]shing` | 159.7 | 5.8 | 150.8 | 133.6 | 27.5x |
| `Huck[a-zA-Z]+|Saw[a-zA-Z]+` | 385.4 | 2.0 | 191.5 | 215.7 | 192.7x |
| `\b\w+nn\b` | 379.1 | 10.2 | 233.2 | 419.8 | 37.2x |
| `[a-q][^u-z]{13}x` | 602.9 | 1.5 | 474.4 | 1079.5 | 401.9x |
| `Tom|Sawyer|Huckleberry|Finn` | 681.3 | 2.9 | 339.6 | 572.5 | 234.9x |
| `(?i)Tom|Sawyer|Huckleberry|Finn` | 652.3 | 38.8 | 433.2 | 598.2 | 16.8x |
| `.{0,2}(Tom|Sawyer|Huckleberry|Finn)` | 2002.6 | 5.8 | 1165.8 | 2509.4 | 345.3x |
| `.{2,4}(Tom|Sawyer|Huckleberry|Finn)` | 2199.5 | 5.8 | 1085.0 | 2767.7 | 379.2x |
| `Tom.{10,25}river|river.{10,25}Tom` | 393.4 | 10.5 | 182.3 | 222.9 | 37.5x |
| `[a-zA-Z]+ing` | 752.6 | 10.7 | 370.9 | 1011.7 | 70.3x |
| `\s[a-zA-Z]{0,12}ing\s` | 260.5 | 14.3 | 280.1 | 448.7 | 18.2x |
| `([A-Za-z]awyer|[A-Za-z]inn)\s` | 425.2 | 41.7 | 287.7 | 409.3 | 10.2x |
| `["'][^"']{0,30}[?!\.]["']` | 34.0 | 7.3 | 159.9 | 165.3 | 4.7x |
| `∞|✓` | 330.5 | 0.7 | 170.9 | 205.4 | 472.1x |
| `\p{Sm}` | 79.3 | 1.2 | 178.8 | 869.3 | 66.1x |
| `(.*?,){13}z` | 13305.2 | 167.7 | 4834.9 | 0.0 | 79.3x |

**結論**: 低速の主因はバックトラック VM 自体ではなく、**「開始位置の絞り込み（prefilter）が無いこと」**です。SRELL が 1〜10ms 台で終えるのは必須リテラル／先頭集合の prefilter を持っているためと考えられます。

---

## 2. ボトルネックの根本原因

1. **先頭検索の fast path が狭すぎる**（`vm.cpp:351-387`）。`START:` で `inst->op` が `Char` / `String` / `CharSet` のときだけ `memmem` による位置ジャンプを行う。`IChar` / `ICharSet` / `Word` / `Alt` / `BeginLoop` / `BeginLookAround` / 先頭 `.*` は対象外。
2. **`Alt` が最初に実行される**（`emit.cpp:470-489`）。`generateAlt` は分岐の前に `AltIns` を置くため、`Huck…|Saw…` のように各分岐が `String("Huck")` に融合されていても最初の命令が `Alt` になり fast path に入らない。リテラル融合（`emit.cpp:413-468`）は「先頭からの連続 `CharNode`」しか対象でない上、`IGNORE_CASE` では無効。
3. **再試行が 1 コードポイントずつ**（`vm.cpp:851-860`）。マッチ失敗ごとに `input.consumeForward()` で1文字だけ進めて再実行するため、開始位置の候補が事実上「全文字」になる。`.*` 前置や `\b` 前置では特に致命的。
4. **必須リテラル解析が無い**。`[a-z]shing` の `shing`、`\b\w+nn\b` の `nn`、`[a-zA-Z]+ing` の `ing`、`(.*?,){13}z` の `z` を使った走査スキップを行っていない。

---

## 3. 改善案一覧（優先順位）

| # | 施策 | 主な効果対象 | 効果 |
|---|---|---|---|
| 1 | 必須リテラル／先頭コードポイント集合の解析＋高速プリフィルタ | `shing`/`ing`/`nn`/`z` 系 | 最大 |
| 2 | 先頭 fast path の一般化（consume-style + locate-only） | `Alt`/ループ/`\b`/`IChar`/キャプチャ先頭 | 大 |
| 3 | アンカー検出（再試行1回化） | `^`/`\A` 先頭 | 中 |
| 4 | リテラル選択肢の radix tree 化 | 全分岐リテラルの `Alt` | 大 |
| 5 | `.*`／遅延量指定子の後方リテラル基準探索 | `.{0,2}(…)`/`(.*?,){13}z` | 大 |
| 6 | Unicode プロパティ名マップの遅延生成／完全ハッシュ | `\p{Sm}` の `mem_inst` | 小 |

本ドキュメントでは **#1** と **#2** を詳細化します。

---

## 4. 改善案1: 必須リテラル／先頭コードポイント集合の解析＋高速プリフィルタ

### 4.1 ねらいと設計判断

コンパイル時に「マッチ開始位置の必要条件」を静的に求め、実行時はその条件を満たす位置まで一気にジャンプします。条件は次の2種類に絞ります。

- **先頭コードポイント集合 `leading`**: マッチの1文字目になり得るコードポイント集合（必ず**上位集合**＝取りこぼし禁止）。
- **必須リテラル `requiredLiteral`**: どんなマッチにも必ず含まれるバイト列（`Alt` なら全分岐共通のもののみ）。

さらに `anchored`（非 multiline の `^`/`\A` で先頭固定）を加えます。

**命令列（`instSeq`）は一切変えない**。`Regex` に解析結果をメタデータとして持たせ、`vm.cpp` の `match()` の外側再試行ループだけを差し替えます。これにより既存のバックトラック VM の意味論・キャプチャ・グローバル走査に手を入れずに済みます。

### 4.2 `Regex` への情報追加（`src/regex/regex.h`）

```cpp
// regex.h : struct Inst; の直後あたりに追加
struct Prefilter {
  /**
   * マッチ先頭になり得るコードポイント集合。
   * 空 = 「不明」。不明ならフィルタせず従来どおり全位置を試す（安全側）。
   */
  CodePointSet leading;

  /**
   * 全マッチに必ず現れるバイト列。空 = なし。
   * 入力全体に対する存在チェック専用（位置合わせには使わない）。
   */
  std::string requiredLiteral;

  /** true なら全マッチが入力先頭（現在オフセット）固定。再試行は1回だけ。 */
  bool anchored{false};

  bool hasLeading() const { return static_cast<bool>(this->leading); }
  bool hasLiteral() const { return !this->requiredLiteral.empty(); }
};
```

`Regex` 本体にメンバとアクセサ、コンストラクタ引数を追加します（`regex.h:34-49`）。

```cpp
class Regex {
private:
  Flag flag;
  unsigned short loopCount;
  unsigned int captureGroupCount;
  FlexBuffer<Inst> instSeq;
  std::vector<Matcher> matchers;
  NamedCaptureGroups named;
  Prefilter prefilter;                    // ← 追加

public:
  Regex(Flag flag, unsigned short loopCount, FlexBuffer<Inst> &&seq,
        std::vector<Matcher> &&matchers, NamedCaptureGroups &&named, unsigned int count,
        Prefilter &&prefilter)            // ← 追加
      : flag(flag), loopCount(loopCount), captureGroupCount(count), instSeq(std::move(seq)),
        matchers(std::move(matchers)), named(std::move(named)),
        prefilter(std::move(prefilter)) {}

  const Prefilter &getPrefilter() const { return this->prefilter; }  // ← 追加
  // 以降のアクセサは既存のまま
};
```

`CodePointSet` は move-only ですが `Regex` は `Optional<Regex>` で move 返却されるため問題ありません。

### 4.3 解析器の実装（新規 `src/regex/analyze.h` / `analyze.cpp`）

char class → コードポイント集合の変換は `CodeGen` の既存ロジック（`generateStrSet` / `toCodePointSet`）を再利用するため、解析は `CodeGen` のメンバとして実装します。まず公開ヘルパを1つ追加します（`emit.h`）。

```cpp
// emit.h : class CodeGen { public: ... } に追加
  /**
   * CharClass / Property を単一コードポイント集合として取り出す。
   * emoji 列や複数文字列（radix）を含む場合は「単純集合に還元できない」ので false。
   * 呼び出し側は false のときプリフィルタを無効化する。
   */
  bool buildCodePointSet(const Node &node, CodePointSetBuilder &out) const;
```

```cpp
// emit.cpp
bool CodeGen::buildCodePointSet(const Node &node, CodePointSetBuilder &out) const {
  if (isa<CharClassNode>(node)) {
    StrSetBuilder setBuilder(this->has(Modifier::IGNORE_CASE));
    this->generateStrSet(setBuilder, 0, node);
    if (!setBuilder.radix.empty() || setBuilder.hasEmoji() || setBuilder.emptySeq) {
      return false; // 文字列/絵文字を含む -> 先頭1コードポイントに還元できない
    }
    out.add(setBuilder.codePoints);
    return true;
  }
  if (isa<PropertyNode>(node)) {
    auto &p = cast<PropertyNode>(node);
    if (p.mayContainString()) {
      return false; // \p{RGI_Emoji} など
    }
    CodePointSetBuilder sub;
    const bool useBuilder = p.isInvert() || this->has(Modifier::IGNORE_CASE);
    this->toCodePointSet(ucp::BuilderOrSet(sub), p);
    if (useBuilder) {
      this->mayBeSimpleCaseFolding(sub);
      if (p.isInvert()) {
        this->complement(sub);
      }
      if (this->has(Modifier::IGNORE_CASE)) {
        sub.foldCase();
      }
    }
    out.add(sub);
    return true;
  }
  return false;
}
```

#### 4.3.1 先頭コードポイント集合（First 集合）

nullable 判定と先頭集合を求める再帰を `CodeGen` の private メソッドとして追加します。**戻り値は「そのノードが空マッチ可能か（nullable）」**、副作用で `out` に先頭集合を足し込みます。

```cpp
// emit.h private: に追加
  bool collectLeading(const Node &node, CodePointSetBuilder &out);
  bool prefilterUnknown_{false};
```

```cpp
// emit.cpp
bool CodeGen::collectLeading(const Node &node, CodePointSetBuilder &out) {
  if (this->prefilterUnknown_) { // 既に不明確定。以降は何もしない
    return true;
  }
  switch (node.getKind()) {
  case NodeKind::Empty:
    return true;                      // 空文字
  case NodeKind::Boundary:            // ^ $ \b \B は一切消費しない
  case NodeKind::LookAround:          // 先読み/後読みは一切消費しない
    return true;
  case NodeKind::Any:
    out.addRange(0, UnicodeUtil::CODE_POINT_MAX);   // 全コードポイント
    return false;
  case NodeKind::Char: {
    const int cp = cast<CharNode>(node).getCodePoint();
    if (this->has(Modifier::IGNORE_CASE)) {
      addFoldedEquivalenceClass(cp, out);           // 4.3.4 参照
    } else {
      out.addRange(cp, cp);
    }
    return false;
  }
  case NodeKind::CharClass:
  case NodeKind::Property:
    if (!this->buildCodePointSet(node, out)) {
      out.clear();
      this->prefilterUnknown_ = true;               // 還元不能 -> フィルタ断念
    }
    return false;
  case NodeKind::BackRef:
    out.clear();
    this->prefilterUnknown_ = true;                 // 先頭がキャプチャ依存 -> 不明
    return false;
  case NodeKind::Group: {
    auto &g = cast<GroupNode>(node);
    if (g.getType() != GroupNode::Type::MODIFIER) {
      return this->collectLeading(*g.getPattern(), out);
    }
    auto newMods = this->modifiers();
    if (auto set = g.getSetModifiers(); set != Modifier::NONE) setFlag(newMods, set);
    if (auto unset = g.getUnsetModifiers(); unset != Modifier::NONE) unsetFlag(newMods, unset);
    this->modifierStack.push(newMods);
    bool nullable = this->collectLeading(*g.getPattern(), out);
    this->modifierStack.pop();
    return nullable;
  }
  case NodeKind::Repeat: {
    auto &r = cast<RepeatNode>(node);
    // 常に本体の先頭集合を足す（min>=1 で本体が先頭を消費し得るため）
    bool innerNullable = this->collectLeading(*r.getPattern(), out);
    return r.getMin() == 0 || innerNullable;        // min==0 なら空マッチ可能
  }
  case NodeKind::Seq: {
    // nullable な要素は「消費せず次へ」も「非空マッチで先頭を取る」も両方あり得る
    for (auto &e : cast<SeqNode>(node).getPatterns()) {
      if (!this->collectLeading(*e, out)) {
        return false;                               // 非 nullable に到達 -> Seq 全体も非 nullable
      }
    }
    return true;                                    // 全要素 nullable
  }
  case NodeKind::Alt: {
    CodePointSetBuilder all;
    bool anyNullable = false;
    for (auto &e : cast<AltNode>(node).getPatterns()) {
      if (!e) { anyNullable = true; continue; }     // 空分岐
      CodePointSetBuilder branch;
      if (this->collectLeading(*e, branch)) anyNullable = true;
      all.add(branch);
    }
    out.add(all);
    return anyNullable;
  }
  }
  return true;
}
```

**ポイント**: `Seq` は「nullable な前置を読み飛ばして、最初の非 nullable 要素（または全要素 nullable）まで先頭集合を union する」必要があります。`a*b` なら `{'a','b'}` を正しく得ます。

#### 4.3.2 必須リテラル

「そのノードの全マッチに必ず含まれるバイト列」を返します。`Alt` は全分岐で同一のものだけ、`Repeat` は `min==0` なら無し、`IGNORE_CASE` の `Char` はバイト一致が保証されないので無しとします。

```cpp
// analyze.cpp (static)
static std::string requiredLiteralOf(const Node &node, const Modifier mods) {
  switch (node.getKind()) {
  case NodeKind::Char:
    if (hasFlag(mods, Modifier::IGNORE_CASE)) return {};  // 大小文字ゆれ -> 保証不可
    {
      char data[4];
      unsigned int len = UnicodeUtil::codePointToUtf8(cast<CharNode>(node).getCodePoint(), data);
      return std::string(data, len);
    }
  case NodeKind::Seq: {
    std::string best;
    for (auto &e : cast<SeqNode>(node).getPatterns()) {
      if (auto s = requiredLiteralOf(*e, mods); s.size() > best.size()) {
        best = std::move(s);                                // 最長の必須リテラルを採用
      }
    }
    return best;
  }
  case NodeKind::Alt: {
    std::string common;
    bool first = true;
    for (auto &e : cast<AltNode>(node).getPatterns()) {
      std::string s = e ? requiredLiteralOf(*e, mods) : std::string();
      if (s.empty()) return {};                             // 分岐に共通リテラル無し
      if (first) { common = std::move(s); first = false; }
      else if (s != common) return {};
    }
    return common;
  }
  case NodeKind::Repeat: {
    auto &r = cast<RepeatNode>(node);
    if (r.getMin() == 0) return {};                         // 0回許容 -> 必須でない
    return requiredLiteralOf(*r.getPattern(), mods);
  }
  case NodeKind::Group: {
    auto &g = cast<GroupNode>(node);
    auto newMods = mods;
    if (g.getType() == GroupNode::Type::MODIFIER) {
      if (auto set = g.getSetModifiers(); set != Modifier::NONE) setFlag(newMods, set);
      if (auto unset = g.getUnsetModifiers(); unset != Modifier::NONE) unsetFlag(newMods, unset);
    }
    return requiredLiteralOf(*g.getPattern(), newMods);
  }
  default:
    return {};                                              // Any/CharClass/Boundary/LookAround/BackRef
  }
}
```

`[a-z]shing` → `"shing"`、`\b\w+nn\b` → `"nn"`、`[a-zA-Z]+ing` → `"ing"`、`(.*?,){13}z` → `"z"` が得られます。`Alt` の `Tom|Sawyer|…` は共通リテラル無しで `{}`。

#### 4.3.3 アンカー

```cpp
static bool startsWithAnchor(const Node &node, const Modifier mods) {
  switch (node.getKind()) {
  case NodeKind::Boundary: {
    auto &b = cast<BoundaryNode>(node);
    return b.getType() == BoundaryNode::Type::START && !hasFlag(mods, Modifier::MULTILINE);
  }
  case NodeKind::Group: {
    auto &g = cast<GroupNode>(node);
    auto newMods = mods;
    if (g.getType() == GroupNode::Type::MODIFIER) {
      if (auto set = g.getSetModifiers(); set != Modifier::NONE) setFlag(newMods, set);
      if (auto unset = g.getUnsetModifiers(); unset != Modifier::NONE) unsetFlag(newMods, unset);
    }
    return startsWithAnchor(*g.getPattern(), newMods);
  }
  case NodeKind::Seq: {
    auto &ps = cast<SeqNode>(node).getPatterns();
    return !ps.empty() && startsWithAnchor(*ps[0], mods);
  }
  default:
    return false;
  }
}
```

#### 4.3.4 ignore-case の先頭集合は「逆像」を取る（重要）

`IChar` の判定は `doSimpleCaseFolding(input) == doSimpleCaseFolding(pattern)` です（`vm.cpp:515-523`）。したがって先頭集合は `fold(c) == fold(cp)` を満たす**全 c の集合（fold の逆像）**でなければなりません。`CodePointSetBuilder::foldCase()` は畳み先しか入れないため、これをそのまま使うと `'A'` を取りこぼします。逆像は「fold 値 → 逆像」の索引を一度だけ作って引きます（既存の「Unicode プロパティ名マップの遅延構築」と同じ発想）。

```cpp
// analyze.cpp (static)
static const std::vector<std::pair<int, int>> &getFoldIndex() {
  static const auto index = [] {
    std::vector<std::pair<int, int>> v;
    v.reserve(UnicodeUtil::CODE_POINT_MAX + 1u);
    for (int cp = 0; cp <= UnicodeUtil::CODE_POINT_MAX; cp++) {
      v.emplace_back(doSimpleCaseFolding(cp), cp);          // (folded, original)
    }
    std::sort(v.begin(), v.end());
    return v;
  }();
  return index;
}

static void addFoldedEquivalenceClass(const int codePoint, CodePointSetBuilder &out) {
  const int folded = doSimpleCaseFolding(codePoint);
  const auto &index = getFoldIndex();
  auto less = [](const std::pair<int, int> &x, const int y) { return x.first < y; };
  auto [first, last] = std::equal_range(index.begin(), index.end(), folded, less);
  for (auto it = first; it != last; ++it) {
    out.addRange(it->second, it->second);
  }
}
```

索引は `~1.1M × 8B ≈ 9MB` の一度きりのグローバルキャッシュです。ignore-case パターンが1つも無ければ `getFoldIndex()` は呼ばれずコストは発生しません（`(?i)Twain` 185ms の `{T,t}` がこれで正しく得られます）。

#### 4.3.5 `CodeGen` への組み込み

```cpp
// emit.h public: に追加
  Prefilter buildPrefilter(const Node &root);
```

```cpp
// emit.cpp
Prefilter CodeGen::buildPrefilter(const Node &root) {
  Prefilter pre;
  this->prefilterUnknown_ = false;
  CodePointSetBuilder leadingBuilder;
  const bool rootNullable = this->collectLeading(root, leadingBuilder);
  pre.requiredLiteral = requiredLiteralOf(root, this->modifiers());
  pre.anchored = startsWithAnchor(root, this->modifiers());

  if (!rootNullable && !this->prefilterUnknown_ && leadingBuilder) {
    pre.leading = leadingBuilder.build();
  } else {
    pre.leading = CodePointSet();   // 空 = 無効。nullable なら全位置が候補なので必ず無効化
  }
  if (rootNullable) {
    pre.requiredLiteral.clear();    // 空マッチ可能なら必須リテラルは存在しない
  }
  return pre;
}
```

`operator()`（`emit.cpp:298-322`）の最後で呼びます。`generate` の後、`tree` を move する前に呼ぶのが安全です。

```cpp
  // finalize
  this->builder.emit<MatchIns>();
  auto flag = tree.getFlag();
  auto count = tree.getCaptureGroupCount();
  auto loopCount = tree.getLoopCount();
  auto prefilter = this->buildPrefilter(*tree.getPattern());                 // ← 追加
  return Regex(flag, loopCount, std::move(this->builder).build(), std::move(this->matchers),
               std::move(tree).takeNamedCaptureGroups(), count, std::move(prefilter));
```

### 4.4 VM への適用（`src/regex/vm.cpp`）

```cpp
// vm.cpp (static, match の近くに配置)
[[nodiscard]] static bool advanceToCandidate(const Regex &re, Input &input) {
  const auto &pf = re.getPrefilter();
  if (pf.anchored) {
    return true;                     // 先頭固定。ジャンプ不要（外側ループも1回で止める）
  }
  const bool hasLeading = pf.hasLeading();
  const bool hasLiteral = pf.hasLiteral();
  if (!hasLeading && !hasLiteral) {
    return true;                     // 情報なし -> 従来動作
  }

  // 必須リテラルは「入力全体に存在するか」の早期判定にだけ使う
  if (hasLiteral &&
      input.remainForward().find(StringRef(pf.requiredLiteral)) == StringRef::npos) {
    input.setIter(input.getEnd());
    return false;                    // 以降どこにもマッチしない
  }

  if (!hasLeading) {
    return true;                     // リテラル存在のみ確認して通常走査
  }

  const auto ref = pf.leading.ref();
  if (isAsciiOnlyCodePointSet(ref)) { // ASCII のみ -> バイト単位のビットマップ走査
    uint64_t bits[2] = {0, 0};
    for (auto [first, last] : ref.getBMPRanges()) {
      for (int c = first; c <= last; c++) {   // 範囲は ASCII 限定
        bits[c >> 6] |= static_cast<uint64_t>(1) << (c & 63);
      }
    }
    while (input.available()) {
      const unsigned char b = static_cast<unsigned char>(*input.getIter());
      if (b < 128 && ((bits[b >> 6] >> (b & 63)) & 1u)) {
        return true;                 // 候補発見（消費しない）
      }
      input.setIter(input.getIter() + 1);   // 非 ASCII は全バイト >=0x80 なので安全に読み飛ばせる
    }
    return false;
  }

  // 非 ASCII を含む集合 -> コードポイント単位で走査
  while (input.available()) {
    const char *iter = input.getIter();
    const int cp = unsafeNextUtf8(iter);
    if (ref.contains(cp)) {
      return true;
    }
    input.consumeForward();
  }
  return false;
}
```

`isAsciiOnlyCodePointSet` は `ref.getBMPSize()` / `getNonBMPRanges()` / `getPackedNonBMPRanges()` を見て「最大コードポイント ≤ 127」を判定する小さなヘルパです（`misc/codepoint_set.hpp` の API で実装可能）。

`match()` への差し込みは、初回試行の前に1回、外側ループの各再試行で呼びます。既存の `START:` fast path（`vm.cpp:351-387`）はそのまま残します。

```cpp
  // ★ 初回試行の位置も候補へ寄せる
  if (!advanceToCandidate(regex, input)) {
    ctx.syncInput(input);
    return MatchStatus::FAIL;
  }
  oldIter = input.getIter();

  // 外側の再試行ループ（vm.cpp:851-860 を置き換え）
  input.setIter(oldIter);
  if (input.available()) {
    input.consumeForward();                 // ★ 必ず1コードポイント進めて無限ループを防ぐ
    if (!advanceToCandidate(regex, input)) { // ★ 次の候補位置へジャンプ
      ctx.syncInput(input);
      return MatchStatus::FAIL;
    }
    oldIter = input.getIter();
    inst = bts.getStartInst();
    ctx.clearCaptures();
    captures = ctx.getCaptures();
    goto START;
  }
  ctx.syncInput(input);
  return MatchStatus::FAIL;
```

**無限ループ防止が肝**です。`[a-z]shing` のように先頭集合に現在位置の文字が含まれる場合、候補を「同じ位置」で返すと進まないので、`consumeForward()` で必ず1コードポイント進めてから候補探索します。

### 4.5 正しさの不変条件

1. **`leading` は上位集合**（false negative 禁止）。還元できないノード（`BackRef`、emoji/複数文字列クラス）に遭遇したら即 `prefilterUnknown_` を立てて `leading` を空（＝無効）に落とす。空は「不明＝フィルタしない」を意味する。
2. **nullable なパターンでは先頭集合フィルタを無効化**。`a?` のように空マッチ可能だと任意位置でマッチするため、`{a}` で絞るとマッチ数を変えてしまう。
3. **ignore-case は逆像**（4.3.4）。`foldCase()` の畳み先集合をそのまま使うと `'A'` を取りこぼす。
4. **`requiredLiteral` は全マッチ共通のみ**。`Alt` では全分岐一致、`Repeat` は `min>=1`、ignore-case の `Char` は不使用。あくまで入力全体の存在チェックに限定し、開始位置合わせには使わない。
5. **入力は常に UTF-8**（`Input::create` が検証）なので、`Char` のコードポイント→UTF-8 バイト列はそのまま `memmem` に使える。
6. **アンカー時は再試行しない**。`anchored` なら `advanceToCandidate` は即 true、外側ループも1回で終わる。

---

## 5. 改善案2: 先頭 fast path の一般化（`vm.cpp:351-387`）

### 5.1 現状の限界

現在の `START:` は、**最初の命令が `Char` / `String` / `CharSet` のときだけ**先読みします。`oldIter` はマッチ開始位置、`input.iter` は先頭トークン通過後です。失敗すると `oldIter` に戻して1コードポイント進め、`goto START` で再走査します。

| パターン | 先頭命令 | 現状 | 遅さ |
|---|---|---|---|
| `[a-z]shing` | `CharSet` | 対象 | ○（159ms は required literal が無いため） |
| `(?i)Twain` | `IChar` | **対象外** | 185ms |
| `Huck…|Saw…`, `Tom|Sawyer|…`, `∞|✓` | `Alt` | **対象外** | 385〜681ms |
| `[a-zA-Z]+ing` | `BeginLoop` | **対象外** | 753ms |
| `\b\w+nn\b` | `Word` | **対象外** | 379ms |
| `([A-Za-z]awyer|…)\s` | `BeginCapture` | **対象外** | 425ms |
| `Tom.{10,25}river|…` | `Alt` | **対象外** | 393ms |

「開始位置を絞れる情報があるのに、先頭命令の形が違うだけで使われない」ことが問題です。ここを2種類の走査に一般化します。

- **consume-style**（既存の延長）: 先頭が「単一の消費トークン」のとき。トークンを探して**消費し**、`inst` をトークン分進める。`Char` / `String` / `CharSet` に加え **`IChar` / `ICharSet`** を対象にする。
- **locate-only**（新規）: 先頭命令が `Alt` / `BeginLoop` / `Word` / `BeginCapture` / `BeginLookAround` などでも、**先頭コードポイント集合**（改善案1の `Prefilter::leading`）だけを使って次の候補位置まで `input.iter` を進め、**命令は再実行**する。境界・キャプチャ・`Alt` の意味論を変えずに済む。

`START:` は「まず consume-style、対象外なら locate-only」の2段構えにします。

### 5.2 コード1: 先頭トークンの消費スキャン

```cpp
enum class LeadingSearch : unsigned char {
  NotApplicable, // 消費トークンではない -> locate-only へ
  Consumed,      // 先頭トークンを発見・消費した（inst はトークン分進んでいる）
  NotFound,      // 以降どこにも候補なし -> 即 BACKTRACK（FAIL に収束）
};

/**
 * 先頭が単一の消費トークン（Char/String/CharSet/IChar/ICharSet）なら、
 * 次の候補まで走査して「トークンを消費」する。成功時:
 *   oldIter    = マッチ開始位置
 *   input.iter = トークン通過後
 *   inst       = トークン分進んだ位置
 */
static LeadingSearch scanLeadingToken(const Inst *&inst, Input &input, const char *&oldIter,
                                      const ArrayRef<Matcher> matchers) {
  switch (inst->op) {
  case OpCode::Char: {
    char data[4];
    const auto &ins = cast<CharIns>(*inst);
    const unsigned int len = UnicodeUtil::codePointToUtf8(ins.getCodePoint(), data);
    const StringRef needle(data, len);
    const auto retPos = input.remainForward().find(needle);
    if (retPos == StringRef::npos) {
      oldIter = input.getEnd();
      return LeadingSearch::NotFound;
    }
    oldIter = input.getIter() + retPos;
    input.setIter(oldIter + needle.size());
    inst += sizeof(CharIns);
    return LeadingSearch::Consumed;
  }
  case OpCode::String: {
    const auto &ins = cast<StringIns>(*inst);
    const StringRef needle = matchers[ins.getIndex()].asStrRef();
    const auto retPos = input.remainForward().find(needle);
    if (retPos == StringRef::npos) {
      oldIter = input.getEnd();
      return LeadingSearch::NotFound;
    }
    oldIter = input.getIter() + retPos;
    input.setIter(oldIter + needle.size());
    inst += sizeof(StringIns);
    return LeadingSearch::Consumed;
  }
  case OpCode::CharSet: {
    const auto &ins = cast<CharSetIns>(*inst);
    const bool invert = ins.invert;
    const auto matcherIndex = ins.getMatcherIndex();
    inst += sizeof(CharSetIns);
    while (input.available()) {
      oldIter = input.getIter();
      if (matchers[matcherIndex].contains(input.consumeForward()) != invert) {
        return LeadingSearch::Consumed;
      }
    }
    oldIter = input.getEnd();
    return LeadingSearch::NotFound;
  }
  // ★ 追加: ignore-case 単一文字。VM の IChar と同じ比較 (vm.cpp:515-523)。
  //   ICharIns には既に doSimpleCaseFolding 済みの値が入っている (emit.cpp:869)。
  case OpCode::IChar: {
    const int folded = cast<ICharIns>(*inst).getCodePoint();
    inst += sizeof(ICharIns);
    while (input.available()) {
      oldIter = input.getIter();
      if (doSimpleCaseFolding(input.consumeForward()) == folded) {
        return LeadingSearch::Consumed;
      }
    }
    oldIter = input.getEnd();
    return LeadingSearch::NotFound;
  }
  // ★ 追加: ignore-case 集合。matcher 側も fold 済み (emit.cpp:787-789)、
  //   入力も fold して照合するので VM の ICharSet と等価 (vm.cpp:554-565)。
  case OpCode::ICharSet: {
    const auto &ins = cast<ICharSetIns>(*inst);
    const bool invert = ins.invert;
    const auto &matcher = matchers[ins.getMatcherIndex()];
    inst += sizeof(ICharSetIns);
    while (input.available()) {
      oldIter = input.getIter();
      if (matcher.contains(doSimpleCaseFolding(input.consumeForward())) != invert) {
        return LeadingSearch::Consumed;
      }
    }
    oldIter = input.getEnd();
    return LeadingSearch::NotFound;
  }
  default:
    return LeadingSearch::NotApplicable;
  }
}
```

`IChar` / `ICharSet` は `doSimpleCaseFolding` を通すだけで、`CharSet` の走査ロジックをそのまま流用できます（`unicode/case_fold.h` は `vm.cpp` 22行目で既に include 済み）。

### 5.3 コード2: 先頭集合による locate-only スキャン

```cpp
[[nodiscard]] static bool isAsciiOnlyCodePointSet(const CodePointSetRef ref) {
  if (ref.getPackedNonBMPSize() || !ref.getNonBMPRanges().empty()) {
    return false;
  }
  const auto bmp = ref.getBMPRanges();
  return bmp.empty() || bmp.back().lastBMP() <= 0x7F;
}

/**
 * 先頭集合に含まれるコードポイントの位置まで input.iter を進める（消費しない）。
 * 見つかれば input.iter は候補位置（= マッチ開始候補）を指す。見つからなければ false。
 */
[[nodiscard]] static bool locateNextCandidate(const Regex &re, Input &input) {
  const auto &pf = re.getPrefilter();
  if (pf.anchored) {
    return true; // 先頭固定。移動不要（再試行も1回で止める）
  }
  const bool hasLeading = pf.hasLeading();
  const bool hasLiteral = pf.hasLiteral();
  if (!hasLeading && !hasLiteral) {
    return true; // 情報なし -> 従来どおり全位置
  }
  // 必須リテラルは「入力全体に存在するか」の早期判定にのみ使う
  if (hasLiteral &&
      input.remainForward().find(StringRef(pf.requiredLiteral)) == StringRef::npos) {
    input.setIter(input.getEnd());
    return false;
  }
  if (!hasLeading) {
    return true;
  }
  const auto ref = pf.leading.ref();
  if (isAsciiOnlyCodePointSet(ref)) { // ASCII のみ -> バイト単位ビットマップ
    uint64_t bits[2] = {0, 0};
    for (auto [first, last] : ref.getBMPRanges()) {
      for (int c = first; c <= last; c++) {
        bits[c >> 6] |= static_cast<uint64_t>(1) << (c & 63);
      }
    }
    while (input.available()) {
      const unsigned char b = static_cast<unsigned char>(*input.getIter());
      if (b < 128 && ((bits[b >> 6] >> (b & 63)) & 1u)) {
        return true;
      }
      input.setIter(input.getIter() + 1); // 非 ASCII は全バイト >= 0x80 なので読み飛ばし可
    }
    return false;
  }
  while (input.available()) { // 非 ASCII を含む -> コードポイント単位
    const char *iter = input.getIter();
    const int cp = unsafeNextUtf8(iter);
    if (ref.contains(cp)) {
      return true;
    }
    input.consumeForward();
  }
  return false;
}
```

`isAsciiOnlyCodePointSet` は `CodePointSetRef` の BMP/非BMP レンジ API（`misc/codepoint_set.hpp:147-161`）で実装できます。ASCII はビットマップ＋バイト走査が最速、非 ASCII はコードポイント走査に切り替えます。

### 5.4 コード3: `START:` と再試行ループの差し替え

```cpp
MatchStatus match(MatchContext &ctx, ObserverPtr<Timer> timer) {
  Input input = ctx.copyInput();
  const Regex &regex = ctx.getRegex();          // ← Prefilter 参照用に追加
  const char *oldIter = input.getIter();
  const Inst *inst = ctx.getInst();
  const auto matchers = ctx.getMatchers();
  LoopState *loopStates = ctx.getLoops();
  ctx.clearCaptures();
  Capture *captures = ctx.getCaptures();
  unsigned int btCount = 0;
  BacktrackStack bts(inst);
  std::string foldBuf;
  if (timer) {
    timer->start();
  }
  // ... 既存の jumpTable 定義 ...

START:
  // 1) 単一の消費トークンなら、それで先読みして消費する
  switch (scanLeadingToken(inst, input, oldIter, matchers)) {
  case LeadingSearch::Consumed:
    break;                       // inst はトークン分進んだ
  case LeadingSearch::NotFound:
    goto BACKTRACK;              // 以降マッチしない
  case LeadingSearch::NotApplicable:
    // 2) 単一トークンでない (Alt/BeginLoop/Word/BeginCapture/...) -> 先頭集合で候補へ
    if (!locateNextCandidate(regex, input)) {
      oldIter = input.getEnd();
      goto BACKTRACK;
    }
    oldIter = input.getIter();   // 候補位置がマッチ開始候補。命令は再実行する
    break;
  }

  bts.push(Backtrack::dummy()); // dummy  （既存どおり）
BACKTRACK:
  while (bts.backtrack(inst, input, captures, loopStates)) {
    // ... 既存の VM 本体 ...
  }

  // 再試行: 1コードポイントだけ進めて START へ戻る（START 側が走査を担当する）
  input.setIter(oldIter);
  if (input.available()) {
    input.consumeForward();      // ★ 必ず進めて無限ループを防ぐ
    oldIter = input.getIter();
    inst = bts.getStartInst();
    ctx.clearCaptures();
    captures = ctx.getCaptures();
    goto START;
  }
  ctx.syncInput(input);
  return MatchStatus::FAIL;
}
```

再試行ループ自体は**1コードポイント前進して `goto START` するだけ**にし、走査は `START` に集約します。これで「最初の1命令が `Alt` / ループ / 境界」でも高速化されます。

### 5.5 先頭集合の供給方法

`locateNextCandidate` は `Prefilter::leading` を読みます。改善案1の解析が、先頭命令の形に依存せず以下を透過的に処理して算出します。

- **`Alt`**: 全分岐の先頭集合の和 → `Huck…|Saw…` は `{H,S}`、`Tom|Sawyer|Huckleberry|Finn` は `{T,S,H,F}`、`∞|✓` は `{∞,✓}`。
- **`BeginLoop`**（`[a-zA-Z]+ing` の `[a-zA-Z]+`）: min≥1 なので本体の先頭集合 `{A-Za-z}`。
- **`Word` / `Start` / `End` / `BeginLookAround` / `BeginCapture` / `Nop` / `ResetCaptures`**: 幅ゼロなので読み飛ばし、後続の消費トークンの集合を採用 → `\b\w+nn\b` は語文字、`([A-Za-z]awyer|…)\s` は `{A-Za-z}`。
- **`IChar` / `ICharSet`**: fold の**逆像**（4.3.4）→ `(?i)Twain` は `{T,t}`、`(?i)Tom|…` は各分岐の逆像の和。

補足: `Any` 先頭や `.{0,2}` のように `leading` が全コードポイント、または nullable の場合は `Prefilter` 側で `leading` を空にして locate-only を無効化します（無駄な `contains` 呼び出しと誤動作を防ぐ）。

### 5.6 正しさのガード

1. **consume-style は「単一の消費トークン」限定**。`IChar` / `ICharSet` は VM と同じ `doSimpleCaseFolding` 比較で照合するので、先読みで消費しても等価。
2. **locate-only は消費しない**。`Word` / `Start` / `End` / `BeginLookAround` / `BeginCapture` / `ResetCaptures` は必ず再実行されるため、境界・キャプチャ・先読みの意味論を変えない。
3. **候補は上位集合**。`leading` に取りこぼしがあってはならない（改善案1の不変条件）。
4. **nullable / 全コードポイントは無効化**。空マッチ可能なパターンは任意位置でマッチするため絞り込み禁止。
5. **無限ループ防止**。再試行では必ず `consumeForward()` してから走査する。
6. **anchored では走査しない**。非 multiline の `^` 先頭は先頭固定なので `locateNextCandidate` は即 true、再試行も1回で終わる。
7. **UTF-8 境界**。consume-style の `memmem` は UTF-8 の自己同期性によりコードポイント途中に一致しない。locate-only は `unsafeNextUtf8` で境界を保つ。

---

## 6. 実装ポイント（該当箇所）

| 施策 | 主なファイル / 行 |
|---|---|
| 先頭検索 fast path | `src/regex/vm.cpp:351-387` |
| 再試行（1コードポイント） | `src/regex/vm.cpp:851-860` |
| リテラル融合 | `src/regex/emit.cpp:413-468` |
| Alt 生成 | `src/regex/emit.cpp:470-489` |
| 文字列集合/radix 生成 | `src/regex/emit.cpp:775-860` |
| Matcher (String/Radix) | `src/regex/matcher.h:123-134,182-186` |
| Regex メタデータ追加 | `src/regex/regex.h:33-62` |
| 文字列検索 (memmem) | `src/misc/string_ref.hpp:129-138,188-192` |
| CodePointSet / Builder | `src/misc/codepoint_set.hpp`, `src/unicode/set_builder.h` |
| ビルド対象追加 | `CMakeLists.txt:259-266`（新規 `analyze.cpp` を追加） |

---

## 7. テスト方針

- **リグレッション**: `test/regex/` の `rematch.cpp` および既存 regex テストを全実行し、`MatchStatus`・マッチ数・キャプチャが不変であることを確認（特に空マッチとグローバル走査 `tools/regex-bench/arsh.cpp::countMatchesIn`）。
- **解析の単体テスト**: `collectLeading` / `requiredLiteralOf` を直接検証。
  - `a*b` → leading `{a,b}`（nullable 前置の読み飛ばし）
  - `a?` → フィルタ無効（rootNullable）
  - `(?i)a` → leading に `{a,A}` を含む（逆像）
  - `(?:a|bc)d` → leading `{a,b}`、literal 無し
  - `\b\w+nn\b` → literal `"nn"`
  - `Tom|Sawyer` → requiredLiteral 空
  - 還元不能クラス（emoji）→ leading 空（無効化）
- **網羅ケース（#2）**: `(?i)` 単一文字/集合、先頭 `Alt`（リテラル/非リテラル混在）、先頭 `\b` / `\B`、先頭キャプチャ、先頭ループ（min=0 / min≥1）、先頭 `^`（anchored）、非ASCII先頭、入力末尾付近での `NotFound`。
- **差分・ファジング**: プリフィルタ有効/無効（無効化フラグで切替）で同一入力を走査し、マッチ列が完全一致することをランダム入力＋`fuzzing/` で検証。
- **性能**: `arsh tools/regex-bench/run.arsh --std-regex --srell --quickjs --hermes --boost --output result.csv` で `arsh [ms]`・`arsh [sp]`・`mem_run` を Before/After 比較。

---

## 8. パターン別の期待効果

| パターン | 現状 arsh | 効くフィルタ | 期待 |
|---|---:|---|---|
| `[a-z]shing` | 159.7ms | leading `{a-z}` + literal `"shing"` | 桁改善（〜10ms 級） |
| `\b\w+nn\b` | 379.1ms | leading 語文字 + literal `"nn"` | 桁改善 |
| `[a-zA-Z]+ing` | 752.6ms | leading 英字 + literal `"ing"` | 桁改善 |
| `\s[a-zA-Z]{0,12}ing\s` | 260.5ms | literal `"ing"`（+ leading `\s`） | 改善 |
| `Huck…|Saw…` | 385.4ms | leading `{H,S}` | 大きく改善 |
| `Tom|Sawyer|Huckleberry|Finn` | 681.3ms | leading `{T,S,H,F}` | 大きく改善 |
| `(?i)Tom|Sawyer|…` | 652.3ms | leading 逆像 | 改善 |
| `(?i)Twain` | 185.2ms | leading 逆像 `{T,t}`（IChar consume） | 大きく改善 |
| `∞|✓` | 330.5ms | leading `{∞,✓}`（非ASCII走査） | 大きく改善 |
| `([A-Za-z]awyer|…)\s` | 425.2ms | BeginCapture → locate `{A-Za-z}` | 大きく改善 |
| `Tom.{10,25}river|…` | 393.4ms | Alt → locate `{T,r}` | 改善 |
| `(.*?,){13}z` | 13305.2ms | literal `"z"` の存在チェック（'z' 不在なら即 FAIL） | ほぼ 0ms 級 |
| `.{0,2}(Tom|…)` / `.{2,4}(…)` | 2002 / 2199ms | 先頭 `.*` で leading=全文字、Alt 共通リテラル無し | **#5 が必要** |

注意点: `[a-z]shing` は現状でも `CharSet` consume が効いており、遅さの主因は「各小文字位置で `String("shing")` を試す」ことなので、**#1 の必須リテラル**が効きます。`(.*?,){13}z` は先頭が nullable で locate が無効、必須リテラルも弱選択（`,`）になるため **#5** が必要です。

---

## 9. 留意点・既知の限界

- `.{0,2}(Tom|Sawyer|Huckleberry|Finn)` のように「可変長の任意消費が先頭にあり、その後が `Alt`」のケースは、先頭集合＝全文字・共通リテラル無しになるため #1/#2 では改善しません。#5（先頭 `.*` の後方にある必須リテラルの位置を基準に候補位置を決める後置アンカー探索）で別途対処が必要です。
- `Alt` の全分岐がリテラルの場合、locate-only よりさらに速い「分岐リテラル radix で直接ジャンプ」は #4 の領分です。
- ignore-case の逆像 fold 索引（約9MB）を避けたい場合は、`IGNORE_CASE` の先頭 `Char` に限り leading フィルタを無効化する保守的フォールバックも選べます（`(?i)Twain` の改善は失われますが安全）。
- 本ドキュメントは分析・設計提案であり、コード変更は行っていません。
