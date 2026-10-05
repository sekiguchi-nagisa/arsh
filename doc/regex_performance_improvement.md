# arsh 正規表現エンジン 改善提案（`result.csv` ベンチマーク分析）

本ドキュメントは、`tools/regex-bench` による18パターンの計測結果（`result.csv`）と、arsh の正規表現エンジン本体（`src/regex/`）の実装分析にもとづく改善提案を、1つの Markdown 文書としてまとめたものです。

---

## 1. ベンチマーク結果の整理

18パターンの合計値（`result.csv` より集計）:

| engine   | 合計時間 [ms] |  score | mem_inst [B] | mem_run [B] |
|----------|--------------:|-------:|-------------:|------------:|
| srell    |         345.7 |     88 |        43016 |       12832 |
| quickjs  |       10779.7 |     44 |         2512 |       14848 |
| hermes   |       11780.3 |     10 |        37496 |       20952 |
| **arsh** |   **22832.3** | **18** |     **7288** |   **21416** |
| boost    |     2002968.8 |     56 |        33984 |       11208 |
| cppstd   |     3036156.9 |      0 |        53816 |       47688 |

arsh の特性は明快に二極化しています。

- **速い（実用十分）**: `Twain` 3.6ms（最速）、`["'][^"']{0,30}[?!\.]["']` 34.0ms、`\p{Sm}` 79.3ms。いずれもトップレベル先頭が `String` / `CharSet` で、`vm.cpp` の先頭検索 fast path が効いている。インスタンスメモリも合計 7KB で全エンジン中最小。
- **遅い（要対策）**: `Huck[a-zA-Z]+|Saw[a-zA-Z]+` 385ms（SRELL比 193倍）、`Tom|Sawyer|…` 681ms（235倍）、`.{0,2}(Tom|…)` 2002ms（345倍）、`[a-z]shing` 160ms、`\b\w+nn\b` 379ms、`[a-zA-Z]+ing` 753ms、`(.*?,){13}z` 13305ms。**例外なく「先頭が `Alt`／ループ／`IChar`／`\b`／文字クラス」のパターン**。

### パターン別（arsh vs SRELL）

| pattern                                  |    arsh | srell | quickjs | hermes | arsh/srell |
|------------------------------------------|--------:|------:|--------:|-------:|-----------:|
| `Twain`                                  |     3.6 |   9.4 |   106.9 |   65.0 |       0.4x |
| `(?i)Twain`                              |   185.2 |   9.4 |   133.8 |   86.3 |      19.7x |
| `[a-z]shing`                             |   159.7 |   5.8 |   150.8 |  133.6 |      27.5x |
| `Huck[a-zA-Z]+\|Saw[a-zA-Z]+`            |   385.4 |   2.0 |   191.5 |  215.7 |     192.7x |
| `\b\w+nn\b`                              |   379.1 |  10.2 |   233.2 |  419.8 |      37.2x |
| `[a-q][^u-z]{13}x`                       |   602.9 |   1.5 |   474.4 | 1079.5 |     401.9x |
| `Tom\|Sawyer\|Huckleberry\|Finn`         |   681.3 |   2.9 |   339.6 |  572.5 |     234.9x |
| `(?i)Tom\|Sawyer\|Huckleberry\|Finn`     |   652.3 |  38.8 |   433.2 |  598.2 |      16.8x |
| `.{0,2}(Tom\|Sawyer\|Huckleberry\|Finn)` |  2002.6 |   5.8 |  1165.8 | 2509.4 |     345.3x |
| `.{2,4}(Tom\|Sawyer\|Huckleberry\|Finn)` |  2199.5 |   5.8 |  1085.0 | 2767.7 |     379.2x |
| `Tom.{10,25}river\|river.{10,25}Tom`     |   393.4 |  10.5 |   182.3 |  222.9 |      37.5x |
| `[a-zA-Z]+ing`                           |   752.6 |  10.7 |   370.9 | 1011.7 |      70.3x |
| `\s[a-zA-Z]{0,12}ing\s`                  |   260.5 |  14.3 |   280.1 |  448.7 |      18.2x |
| `([A-Za-z]awyer\|[A-Za-z]inn)\s`         |   425.2 |  41.7 |   287.7 |  409.3 |      10.2x |
| `["'][^"']{0,30}[?!\.]["']`              |    34.0 |   7.3 |   159.9 |  165.3 |       4.7x |
| `∞\|✓`                                  |   330.5 |   0.7 |   170.9 |  205.4 |     472.1x |
| `\p{Sm}`                                 |    79.3 |   1.2 |   178.8 |  869.3 |      66.1x |
| `(.*?,){13}z`                            | 13305.2 | 167.7 |  4834.9 |    0.0 |      79.3x |

**結論**: 低速の主因はバックトラック VM 自体ではなく、**「開始位置の絞り込み（prefilter）が無いこと」**です。SRELL が 1〜10ms 台で終えるのは必須リテラル／先頭集合の prefilter を持っているためと考えられます。

---

## 2. ボトルネックの根本原因

1. **先頭検索の fast path が狭すぎる**（`vm.cpp:311-379`）。`START:` で `inst->op` が `Char` / `String` / `CharSet` のときだけ `memmem` による位置ジャンプを行う。`IChar` / `ICharSet` / `Word` / `Alt` / `BeginLoop` / `BeginLookAround` / 先頭 `.*` は対象外。
2. **`Alt` が最初に実行される**（`emit.cpp:470-489`）。`generateAlt` は分岐の前に `AltIns` を置くため、`Huck…|Saw…` のように各分岐が `String("Huck")` に融合されていても最初の命令が `Alt` になり fast path に入らない。リテラル融合（`emit.cpp:413-468`）は「先頭からの連続 `CharNode`」しか対象でない上、`IGNORE_CASE` では無効。
3. **再試行が 1 コードポイントずつ**（`vm.cpp:899-908`）。マッチ失敗ごとに `input.consumeForward()` で1文字だけ進めて再実行するため、開始位置の候補が事実上「全文字」になる。`.*` 前置や `\b` 前置では特に致命的。
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

本ドキュメントでは **#1** と **#2** を詳細化します（第4章・第5章）。続く第6章〜第9章で **#3**〜**#6** を、同じ粒度で具体的なコードを示しながら詳細化します。

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

> ここでは「先頭要素そのもの」だけを見る最小版を示しました。`(?:^)a` や `\b^a` のように**幅ゼロ要素の後ろに `^` が来る**ケースまで拾う一般化と、multiline の `^` を行頭へジャンプする版は **6.2 / 6.3** で詳述します。

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

`match()` への差し込みは、初回試行の前に1回、外側ループの各再試行で呼びます。既存の `START:` fast path（`vm.cpp:311-379`）はそのまま残します。

```cpp
  // ★ 初回試行の位置も候補へ寄せる
  if (!advanceToCandidate(regex, input)) {
    ctx.syncInput(input);
    return MatchStatus::FAIL;
  }
  oldIter = input.getIter();

  // 外側の再試行ループ（vm.cpp:899-908 を置き換え）
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

## 5. 改善案2: 先頭 fast path の一般化（`vm.cpp:311-379`）

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

## 6. 改善案3: アンカー検出（再試行の1回化）

### 6.1 ねらい

先頭が「非 multiline の `^`」で固定されているパターンは、候補開始位置が**入力先頭ただ1つ**です。ところが現状は、失敗のたびに1コードポイントずつ進めて再試行します（`vm.cpp:899-908`）。`StartIns` は実行のたびに「入力先頭でない」と判定して `BACKTRACK` するため、無駄な再試行が入力長ぶん発生します。

改善案1の `Prefilter::anchored`（4.2 / 4.3.3）が判定を持ちますが、4.3.3 の `startsWithAnchor` は「ルートの直接の先頭要素」しか見ません。本節では判定を**幅ゼロ要素の読み飛ばし**まで一般化し、あわせて **multiline の `^` も行頭へジャンプ**する実装を示します。

### 6.2 アンカー判定の一般化

`(?:^)a`・`\b^a`・`(?=x)^a` のように `^` の手前に幅ゼロの要素だけが並ぶ場合は anchored です。逆に `a^b` は入力消費の後ろに `^` があるため anchored ではありません。判定は次の2つのヘルパで表現できます。

```cpp
// analyze.cpp

/** 入力もキャプチャも一切消費しない（= 幅が常にゼロ）ノードか。 */
static bool isAlwaysZeroWidth(const Node &node) {
  switch (node.getKind()) {
  case NodeKind::Empty:
  case NodeKind::Boundary:   // ^ $ \b \B
  case NodeKind::LookAround: // (?=..) (?!..) (?<=..) (?<!..)
    return true;
  case NodeKind::Repeat: // max==0 なら本体は一度も実行されない
    return cast<RepeatNode>(node).getMax() == 0;
  case NodeKind::Group:
    return isAlwaysZeroWidth(*cast<GroupNode>(node).getPattern());
  case NodeKind::Seq:
    for (auto &e : cast<SeqNode>(node).getPatterns()) {
      if (!isAlwaysZeroWidth(*e)) return false;
    }
    return true;
  case NodeKind::Alt:
    for (auto &e : cast<AltNode>(node).getPatterns()) {
      if (e && !isAlwaysZeroWidth(*e)) return false;
    }
    return true;
  default: // Any / Char / CharClass / Property / BackRef
    return false;
  }
}

/** 先頭の幅ゼロ列を読み飛ばし、その途中に非 multiline の ^ があれば true。 */
static bool startsWithAnchor(const Node &node, const Modifier mods) {
  switch (node.getKind()) {
  case NodeKind::Boundary:
    return cast<BoundaryNode>(node).getType() == BoundaryNode::Type::START &&
           !hasFlag(mods, Modifier::MULTILINE);
  case NodeKind::Group: {
    auto &g = cast<GroupNode>(node);
    Modifier newMods = mods;
    if (g.getType() == GroupNode::Type::MODIFIER) {
      if (auto set = g.getSetModifiers(); set != Modifier::NONE) setFlag(newMods, set);
      if (auto unset = g.getUnsetModifiers(); unset != Modifier::NONE) unsetFlag(newMods, unset);
    }
    return startsWithAnchor(*g.getPattern(), newMods);
  }
  case NodeKind::Seq:
    for (auto &e : cast<SeqNode>(node).getPatterns()) {
      if (startsWithAnchor(*e, mods)) return true;
      if (!isAlwaysZeroWidth(*e)) return false; // 入力消費に到達 -> 以降は anchored でない
    }
    return false;
  case NodeKind::Alt: // 全分岐が anchored のときだけ anchored
    for (auto &e : cast<AltNode>(node).getPatterns()) {
      if (!e || !startsWithAnchor(*e, mods)) return false;
    }
    return true;
  default:
    return false;
  }
}
```

> `(?=x)^a` は先読みを幅ゼロとして読み飛ばし、後続の `^` で anchored と判定されます。先読みの中身が anchored かどうかは見ません（先読みは位置を消費しないため不要）。

`^` は次の命令列になります（`redump -d` の実測）。

```text
^Twain
  0: Start(multiline=false)
  2: String(index=0)
  5: Match
```

### 6.3 multiline `^` の行頭制限

multiline の `^` は「行頭（直前が改行）」でも成立します。このケースは anchored（1回で終了）にはできませんが、**候補を全行頭に限定**できます。

```cpp
// analyze.cpp : startsWithAnchor と同型。START のとき MULTILINE を要求するだけの違い。
static bool startsWithLineAnchor(const Node &node, const Modifier mods) {
  switch (node.getKind()) {
  case NodeKind::Boundary:
    return cast<BoundaryNode>(node).getType() == BoundaryNode::Type::START &&
           hasFlag(mods, Modifier::MULTILINE); // ← ここだけ異なる
  case NodeKind::Group: {
    auto &g = cast<GroupNode>(node);
    Modifier newMods = mods;
    if (g.getType() == GroupNode::Type::MODIFIER) {
      if (auto set = g.getSetModifiers(); set != Modifier::NONE) setFlag(newMods, set);
      if (auto unset = g.getUnsetModifiers(); unset != Modifier::NONE) unsetFlag(newMods, unset);
    }
    return startsWithLineAnchor(*g.getPattern(), newMods);
  }
  case NodeKind::Seq:
    for (auto &e : cast<SeqNode>(node).getPatterns()) {
      if (startsWithLineAnchor(*e, mods)) return true;
      if (!isAlwaysZeroWidth(*e)) return false;
    }
    return false;
  case NodeKind::Alt:
    for (auto &e : cast<AltNode>(node).getPatterns()) {
      if (!e || !startsWithLineAnchor(*e, mods)) return false;
    }
    return true;
  default:
    return false;
  }
}
```

`Prefilter` 側は既存メンバに1つ足すだけです（`lineAnchored`）。

```cpp
struct Prefilter {
  CodePointSet leading;
  std::string requiredLiteral;
  bool anchored{false};
  bool lineAnchored{false}; // ← 追加: multiline の ^ を先頭に持つ
  // ...
};
```

### 6.4 VM 側: 候補をアンカー位置へ限定する

4.4 で定義した `advanceToCandidate` の先頭を次のように置き換えます（`anchored` の扱いが変わります）。

```cpp
// vm.cpp : 4.4 の advanceToCandidate をこの版に差し替える
[[nodiscard]] static bool advanceToCandidate(const Regex &re, Input &input) {
  const auto &pf = re.getPrefilter();

  // 非 multiline の ^: 候補は入力先頭のみ。
  // 先頭なら試行し、再試行（= 1コードポイント前進後）では即 false を返して FAIL に収束させる。
  if (pf.anchored) {
    return input.isBegin();
  }

  // multiline の ^: 候補は「行頭」のみ。
  if (pf.lineAnchored) {
    if (input.isBegin() || isLineTerminator(input.prev())) {
      return true; // 現在位置がすでに行頭
    }
    while (input.available()) {
      if (isLineTerminator(input.consumeForward()) && input.available()) {
        return true; // 改行の直後 = 行頭
      }
    }
    return false; // 以降に行頭なし
  }

  // ... 4.4 の leading / requiredLiteral の処理をそのまま続ける ...
}
```

`isLineTerminator` は `matcher.h:27-29` の既存ヘルパをそのまま使えます。multiline では行頭が複数あるため外側の再試行ループは回しますが、`StartIns` を満たさない位置での試行が消えます。非 multiline では再試行そのものが1回で止まります。

### 6.5 正しさのガード

1. **`anchored` の再試行は「先頭のみ」**。`input.isBegin()` で判定するので、再試行位置では必ず false になり無限ループしない。
2. **幅ゼロの判定は保守的に**。`isAlwaysZeroWidth` は「絶対に消費しない」ノードだけを true にする。`a?` のような nullable な消費は false（消費し得るため読み飛ばせない）。
3. **`Alt` は全分岐一致が条件**。1つでも非 anchored な分岐があれば false。
4. **`\b^a` のような「位置制約 + アンカー」**は、`^` が入力を消費しないため anchored と判定してよい（`^` が開始位置を先頭に固定するため）。
5. **`\A` は arsh に存在しない**。`u`/`v` モードの `\A` はエラー、`BMP` モードでは文字 `A` になる（`parser.cpp` の `isSyntaxChar` に `A` が無いため）。したがって「`\A` 相当」= 非 multiline の `^` のみを対象にする。

---

## 7. 改善案4: 先頭 `Alt` のリテラル radix 化

### 7.1 現状の問題

`Huck[a-zA-Z]+|Saw[a-zA-Z]+` のコンパイル結果（`redump -d` の実測）は次のとおりです。

```text
Huck[a-zA-Z]+|Saw[a-zA-Z]+
   0: Alt(second=36)
   5: String(index=0)        # "Huck"
   8: BeginLoop(loopIndex=0, greedy=true, min=1, ...)
  ...
  36: String(index=2)        # "Saw"
  ...
```

各分岐の先頭リテラルは既に `String` に融合済み（`emit.cpp:413-468`）ですが、**最初の命令が `Alt`** なので改善案2の consume-style には入らず、locate-only（先頭集合 `{H,S}`）で候補位置まで進んだ後、候補ごとに `String` を分岐数ぶん試すことになります。

```text
Tom|Sawyer|Huckleberry|Finn
   0: Alt(second=13)
   5: String(index=0)        # "Tom"
   8: Jump(target=42)
  13: Alt(second=26)
  18: String(index=1)        # "Sawyer"
   ...
```

改善案4は、この**先頭 `Alt` の分岐リテラルを1つの radix tree にまとめ、候補位置ごとに「どの分岐が起動するか」を1回の木走査で決める**ものです。木走査は位置ごとに O(1) に近く、分岐数ぶんの `String` 比較が消えます。さらに radix は「先頭リテラルに一致しない位置」を弾くので、先頭集合 prefilter（近似）より**厳密**な候補判定としても使えます。

### 7.2 設計判断

- 命令列は変えず、`Prefilter` に「先頭 Alt の radix matcher インデックス + 分岐先オフセット表」を持たせる（改善案1と同じ方針）。
- 適用条件は **先頭 `Alt` の全分岐がリテラル開始**かつ**キーが互いに prefix-free**であること。prefix-free なら位置で一致するキーは高々1つなので、`PackedRadixTree::findLongestMatched` の結果が一意に分岐へ対応します。
- 分岐が重複キー（例 `ab|ab`）を持つ場合、radix は同一キーを1ノードにまとめてしまい分岐先が曖昧になります。`RadixTree::add` は重複挿入で `AddStatus::ADDED` を返すため、これを検出したら最適化を諦めて既存の `generateAlt` にフォールバックします。
- `IGNORE_CASE` も対象にできます。radix のキーを fold 済みで登録し、実行時は `findLongestMatched` に `caseFold=true` を渡します（`vm.cpp:229-259` の既存実装がそのまま使えます）。
- 分岐数の上限は radix の property（1 バイト）に収まる範囲（255）とします。

### 7.3 解析: 分岐リテラルの収集

```cpp
// analyze.cpp

/**
 * 指定 Seq の先頭から連続する CharNode を UTF-8 連結して返す。
 * 連続が 1 文字でも、CharNode 以外に当たったらそこで止める。
 */
static std::string leadingCharLiteral(const SeqNode &seq, const bool ignoreCase) {
  std::string lit;
  for (auto &e : seq.getPatterns()) {
    if (e->getKind() != NodeKind::Char) {
      break;
    }
    if (ignoreCase) {
      return {}; // IGNORE_CASE は generateSeq も融合しない（7.5 参照）
    }
    char data[4];
    const unsigned int len = UnicodeUtil::codePointToUtf8(cast<CharNode>(*e).getCodePoint(), data);
    lit.append(data, len);
  }
  return lit;
}

/**
 * 先頭 Alt の各分岐の先頭リテラルを集める。
 * 全分岐が集まり、かつキーが prefix-free なら true。
 *
 * radix にそのまま登録できる形（キー, 分岐先オフセット）を返す。
 */
static bool collectLeadingAltLiterals(const AltNode &alt, const bool ignoreCase,
                                      std::vector<std::pair<std::string, uint32_t>> &out) {
  for (auto &branch : alt.getPatterns()) {
    if (!branch) return false; // 空分岐（\q{} 等）は対象外
    const Node *p = branch.get();
    // 幅ゼロ要素を読み飛ばして最初の実体を得る
    while (p->getKind() == NodeKind::Group) {
      p = cast<GroupNode>(*p).getPattern().get();
    }
    if (p->getKind() != NodeKind::Seq) return false;
    std::string lit = leadingCharLiteral(cast<SeqNode>(*p), ignoreCase);
    if (lit.empty()) return false;
    out.emplace_back(std::move(lit), 0); // 分岐先は generate 時に埋める
  }
  // prefix-free の確認（片方が他方の接頭辞だと findLongestMatched が一意に定まらない）
  for (unsigned int i = 0; i < out.size(); i++) {
    for (unsigned int j = 0; j < out.size(); j++) {
      if (i == j) continue;
      if (out[i].first.rfind(out[j].first, 0) == 0) {
        return false;
      }
    }
  }
  return true;
}
```

> 実際には `String` 融合（`generateSeq`、`emit.cpp:413-468`）とまったく同じロジックを再利用する方が確実です。`SeqNode` の先頭から連続する `CharNode` を UTF-8 連結し、そのバイト列をキーにします。連続が 1 文字だけでも（`T` + `om` の `T` など）キーとして成立します。`NON_CAPTURE` / `MODIFIER` グループは透過して読み飛ばして良いですが、`BeginCapture` / `Word` / `LookAround` は位置を検証するため読み飛ばしてはなりません（7.6 の 5）。

### 7.4 VM 側: 先頭 Alt の分岐選択

```cpp
// vm.cpp

enum class AltSearch : unsigned char {
  NotApplicable, // 先頭 Alt ではない -> 既存の Alt 実行
  NotFound,      // どの分岐リテラルも一致しない -> 即 BACKTRACK
  Jumped,        // 一致した分岐へジャンプした（prefix は消費済み）
};

/**
 * 先頭 Alt の radix を現在位置で走査し、一致した分岐へジャンプする。
 * 一致しない場合は「この開始位置ではマッチしない」ことが確定する。
 *
 * foldBuf は呼び出し側の match() が持つ作業用文字列を再利用する。
 */
static AltSearch scanLeadingAlt(const Regex &re, const Inst *&inst, Input &input,
                                const ArrayRef<Matcher> matchers, const Inst *startInst,
                                std::string &foldBuf) {
  const auto &pf = re.getPrefilter();
  if (!pf.hasLiteralAlt()) {
    return AltSearch::NotApplicable;
  }
  if (inst != startInst) { // 先頭 Alt は match の先頭でのみ意味を持つ
    return AltSearch::NotApplicable;
  }
  const auto &alt = pf.literalAlt;
  const PackedRadixTree tree = matchers[alt.matcherIndex].asRadixTree();
  const auto [size, property] =
      findLongestMatched(tree, input.remainForward(), foldBuf, alt.ignoreCase);
  if (!property || size == 0) {
    return AltSearch::NotFound;
  }
  const uint32_t target = alt.branchTargets[property - 1];
  input.setIter(input.getIter() + size); // 先頭リテラルを消費
  inst = startInst + target;             // 一致した分岐本体の先頭へ
  return AltSearch::Jumped;
}
```

`findLongestMatched` は `vm.cpp:229-259` の既存実装（`foldBuf` を再利用）をそのまま使えます。`input.remainForward()` は「現在位置からの残り全体」なので、prefix-free なキーなら一致は高々1つに定まります。

`START:` は「consume-style（改善案2）→ 先頭 Alt radix（改善案4）→ locate-only（改善案2）」の3段になります。

```cpp
// vm.cpp : match() の START:
START:
  switch (scanLeadingToken(inst, input, oldIter, matchers)) {
  case LeadingSearch::Consumed:
    break;
  case LeadingSearch::NotFound:
    oldIter = input.getEnd();
    goto BACKTRACK;
  case LeadingSearch::NotApplicable:
    // 1) 先頭 Alt の分岐リテラル radix を試す
    {
      const char *matchStart = input.getIter();         // ← 消費前を退避
      switch (scanLeadingAlt(regex, inst, input, matchers, bts.getStartInst(), foldBuf)) {
      case AltSearch::Jumped:
        oldIter = matchStart;                           // ← マッチ開始位置は消費前
        break;                                          // 分岐本体へジャンプ済み
      case AltSearch::NotFound:
        oldIter = input.getEnd();
        goto BACKTRACK;                                 // どの分岐リテラルも一致しない
      case AltSearch::NotApplicable:
        // 2) どちらでもない -> 先頭集合で候補へ
        if (!locateNextCandidate(regex, input)) {
          oldIter = input.getEnd();
          goto BACKTRACK;
        }
        oldIter = input.getIter();
        break;
      }
    }
    break;
  }
  bts.push(Backtrack::dummy());
  goto BACKTRACK;
```

> `scanLeadingAlt` が prefix を消費した場合でも、`oldIter`（= マッチ開始位置。`captures[0].offset` に使う）は**消費前**でなければなりません。そこで `matchStart` を退避してから分岐リテラルを消費し、ジャンプ後に `oldIter = matchStart;` とします。

### 7.5 `IGNORE_CASE` の扱い

`(?i)Tom|Sawyer|Huckleberry|Finn` は `IGNORE_CASE` のため `generateSeq` のリテラル融合が無効で、各分岐は `IChar` の並びになります（`redump -d` の実測）。

```text
Tom|Sawyer (modifier=i)
   0: Alt(second=25)
   5: IChar(codePoint=U+0074:t)
  10: IChar(codePoint=U+006F:o)
  15: IChar(codePoint=U+006D:m)
  ...
```

この場合も radix 化は可能です。`IChar` は `doSimpleCaseFolding(input) == folded` で比較する（`vm.cpp:563-571`）ので、**キーを `doSimpleCaseFolding` 済みのバイト列で登録**し、実行時は `findLongestMatched(..., caseFold=true)` で入力を fold して照合すれば等価です。単純折り畳みはコードポイントごとに 1:1 で、UTF-8 バイト長も変わらないため、`size` をそのまま消費バイト数に使えます。

```cpp
// analyze.cpp : IChar 並びの先頭リテラルを fold 済みキーとして連結する
static std::string leadingFoldedLiteral(const SeqNode &seq) {
  std::string lit;
  for (auto &e : seq.getPatterns()) {
    if (e->getKind() != NodeKind::Char) {
      break; // 連続する CharNode だけを連結
    }
    const int cp = doSimpleCaseFolding(cast<CharNode>(*e).getCodePoint());
    char data[4];
    const unsigned int len = UnicodeUtil::codePointToUtf8(cp, data);
    lit.append(data, len);
  }
  return lit;
}
```

`leadingFoldedLiteral` は 7.3 の `leadingCharLiteral` と同型で、**各文字を `doSimpleCaseFolding` してから連結する**点だけが異なります。`IChar` は `emitCharIns`（`emit.cpp:868-869`）で既に fold 済みの値が入っているため、解析側で同じ fold を適用すればキーが一致します。`(?i)Twain` の先頭 `[Tt]` は 1 文字ずつ `IChar` になるため、`?i` 付きの先頭 Alt も同じ扱いで radix 化できます（`(?i)Tom|Sawyer|…` は 4 分岐の fold 済みキー `tom` / `sawyer` / … になります）。

### 7.6 正しさのガード

1. **prefix-free 必須**。キーが互いの接頭辞になると、`findLongestMatched` が返す「最長一致」1つでは分岐を取りこぼす。prefix-free を確認できないときは適用しない。
2. **重複キーは不適用**。radix は同一キーを1ノードにまとめるため、分岐先が曖昧になる。
3. **分岐先オフセットは `instSeq` 先頭基準**。既存の `AltIns` と同じ基準を使う（`bts.getStartInst() + target`）。
4. **マッチ開始位置は消費前**。prefix を消費しても `oldIter` は消費前を指すこと（`captures[0].offset` のため）。
5. **先頭 Alt に限定**。`BeginCapture` / `Word` / `LookAround` が先頭にある場合、それらを飛ばして消費してはならない（検証が抜ける）。`NON_CAPTURE` / `MODIFIER` グループだけは透過して良い。
6. **空分岐は不適用**。`\q{}` を含む Alt は `AltIns` + `CharSet` 構成になるため対象外。

---

## 8. 改善案5: `.*`／遅延量指定子の後方リテラル基準探索（後置アンカー）

### 8.1 ねらい

`.{0,2}(Tom|Sawyer|Huckleberry|Finn)` と `(.*?,){13}z` は、**先頭が可変長の任意消費**で、先頭集合＝全コードポイント・共通リテラル無しになるため #1/#2 では改善しません。しかし**後方に必須リテラル**があります。

| パターン | 後方の必須リテラル | 前置が消費し得る最大 |
|---|---|---|
| `.{0,2}(Tom\|Sawyer\|Huckleberry\|Finn)` | `{Tom,Sawyer,Huckleberry,Finn}`（各分岐） | 2 コードポイント |
| `(.*?,){13}z` | `z` | 無制限（ただし `.` は改行を跨がない） |

発想は「必須リテラル `L` の出現位置を基準に、そこから**前置が届き得る範囲**だけを開始候補にする」ことです。

### 8.2 解析: 後置アンカーの検出

```cpp
// analyze.h
struct SuffixAnchor {
  /** 必須リテラル（UTF-8 バイト列）。各 ALT 分岐ごとに1つ持つ。 */
  std::vector<std::string> literals;

  /**
   * リテラルより前に消費され得る最大バイト数。
   * 例えば .{0,2} なら 2 コードポイント <= 8 バイト。
   * 無制限（.* など）の場合は UINT32_MAX。
   */
  unsigned int maxPrefixBytes{UINT32_MAX};

  /**
   * true: 前置が改行を跨がない（AnyExceptNL）ため、マッチは同一行内に閉じる。
   * 開始候補を行内に限定できる。
   */
  bool sameLine{true};

  /**
   * 前置が「リテラルを最低 minRepeats 回以上、かつ特定の区切り区間で」消費するパターン
   * （(.*?,){13} の 13 のような下限）。0 は未使用。
   * minRepeats > 0 のときは、リテラル手前までに minRepeats 個の構造が必要で、
   * 開始候補をさらに絞れる。
   */
  unsigned int minRepeats{0};

  bool has() const { return !this->literals.empty(); }
};

/** ルート先頭の「任意消費 + 必須リテラル」構造を検出する。 */
static bool findSuffixAnchor(const Node &node, const Modifier mods, SuffixAnchor &out);
```

検出の骨子は「先頭から幅ゼロ要素を読み飛ばして可変長消費（`Repeat(Any/AnyExceptNL/CharClass)` または `Repeat(CharClass, min=0)`）を見つけ、その直後（または Alt の各分岐直後）のリテラルを集める」です。

```cpp
// analyze.cpp（骨子）
static bool findSuffixAnchor(const Node &node, const Modifier mods, SuffixAnchor &out) {
  const Node *p = &node;
  Modifier curMods = mods;
  unsigned int maxPrefix = 0;

  // 1) 前置の可変長消費を読み飛ばす
  while (true) {
    if (p->getKind() == NodeKind::Group) {
      auto &g = cast<GroupNode>(*p);
      if (g.getType() == GroupNode::Type::MODIFIER) {
        if (auto set = g.getSetModifiers(); set != Modifier::NONE) setFlag(curMods, set);
        if (auto unset = g.getUnsetModifiers(); unset != Modifier::NONE) unsetFlag(curMods, unset);
      }
      p = g.getPattern().get();
      continue;
    }
    if (p->getKind() != NodeKind::Seq) return false;
    auto &ps = cast<SeqNode>(*p).getPatterns();
    auto iter = ps.begin();
    for (; iter != ps.end(); ++iter) {
      const Node &e = **iter;
      if (e.getKind() == NodeKind::Repeat) {
        auto &r = cast<RepeatNode>(e);
        // . や文字クラスの可変長消費のみ対象
        if (!isAnyOrClass(*r.getPattern())) break;
        if (r.isUnlimited()) {
          // .* / .*? のような無制限消費
          maxPrefix = UINT32_MAX;
          out.sameLine = !hasFlag(curMods, Modifier::DOT_ALL) &&
                         isAnyExceptNL(*r.getPattern());
          if (r.getMin() > 0) out.minRepeats = r.getMin();
        } else {
          maxPrefix += r.getMax() * 4; // 1コードポイント最大4バイトで保守的に見積もる
        }
        continue;
      }
      break; // 消費でない要素に到達
    }
    if (iter == ps.end()) return false;
    // 2) 直後のリテラルを集める（Alt なら全分岐）
    return collectRequiredLiterals(**iter, curMods, maxPrefix, out);
  }
}
```

### 8.3 VM 側: 後置アンカーによる開始候補の限定

前置が**有界**（`maxPrefixBytes != UINT32_MAX`）の場合、各リテラル出現 `[a, b)` に対し開始候補は `[a - maxPrefixBytes, a]` です。すべての出現の和集合が候補集合になります。

```cpp
// vm.cpp

/**
 * 後置アンカー（有界プレフィックス）を使って、現在位置以降で最初の開始候補へ進める。
 * 候補が無ければ false（= 残りに入力なし）。
 */
[[nodiscard]] static bool locateBySuffixAnchor(const Prefilter &pf, Input &input) {
  const auto &sa = pf.suffixAnchor;
  const char *cur = input.getIter();
  const char *best = input.getEnd();
  for (auto &lit : sa.literals) {
    const StringRef needle(lit);
    // 出現の探索開始位置: 候補窓が cur を含み得る最も早い位置
    const char *from = cur;
    if (sa.maxPrefixBytes != UINT32_MAX && from - input.getBegin() > sa.maxPrefixBytes) {
      from -= sa.maxPrefixBytes;
    } else {
      from = input.getBegin();
    }
    const StringRef haystack(from, static_cast<size_t>(input.getEnd() - from));
    auto retPos = haystack.find(needle);
    while (retPos != StringRef::npos) {
      const char *occ = from + retPos;
      const char *winLo = occ;
      if (sa.maxPrefixBytes != UINT32_MAX && occ - input.getBegin() > sa.maxPrefixBytes) {
        winLo = occ - sa.maxPrefixBytes;
      } else {
        winLo = input.getBegin();
      }
      const char *winHi = occ + needle.size(); // 後置アンカーは開始より後ろ
      if (sa.sameLine && !sameLineRange(winLo, winHi)) {
        // 前置が改行を跨げない -> この窓は無効。次の出現へ
        retPos = haystack.find(needle, retPos + 1);
        continue;
      }
      if (winHi >= cur) {
        best = std::min(best, std::max(cur, winLo));
        break; // この literal で最良候補を確定
      }
      retPos = haystack.find(needle, retPos + 1);
    }
  }
  if (best == input.getEnd() && !containsCandidate(pf, input)) {
    return false;
  }
  input.setIter(best);
  return true;
}
```

補助ヘルパは小さなユーティリティです。

```cpp
// vm.cpp

/** [lo, hi) の間に改行が無いか。同じ行に閉じているかの判定に使う。 */
[[nodiscard]] static bool sameLineRange(const char *lo, const char *hi) {
  return memchr(lo, '\n', static_cast<size_t>(hi - lo)) == nullptr;
}

/** 出現位置 t から行頭（直前の '\n' の直後、無ければ入力先頭）を返す。 */
[[nodiscard]] static const char *findLineStart(const char *t, const char *inputBegin) {
  while (t > inputBegin && t[-1] != '\n') {
    t--;
  }
  return t;
}

/** [lo, hi) にバイト ch が現れる回数。 */
[[nodiscard]] static unsigned int countByte(const char *lo, const char *hi, const char ch) {
  unsigned int n = 0;
  for (; lo != hi; lo++) {
    if (*lo == ch) n++;
  }
  return n;
}
```

`best` は「現在位置以降で最も早い開始候補」です。`best == input.getEnd()` かつ候補が1つも無い場合だけ false を返します。`locateBySuffixAnchor` は `maxPrefixBytes != UINT32_MAX`（有界）専用で、無界のときは 8.4 の `locateByMinRepeatAnchor` を使います。両者は `Prefilter` で排他に選びます。

### 8.4 無界プレフィックス: `(.*?,){13}z` の min-repeat 制約

`(.*?,){13}z` の前置は無界なので、単純な窓では `[行頭, z]` となり絞れません。ここで効くのが **min-repeat 制約**です。

- マッチは必ず `z` で**終わる**。
- マッチは `.` の並びなので**改行を跨がない**（同一行内で完結）。
- `(.*?,){13}` は「13 個の区間」を消費するので、マッチ区間内に**少なくとも 13 個のカンマ**が必要。

したがって、`z` の位置 `t` に対して開始候補は `[行頭(t), t から逆向きに 13 番目のカンマの位置]` に限られます。この条件を満たす区間が1つも無ければ、マッチは存在せず即 `FAIL` です。

```cpp
// vm.cpp : 無界プレフィックス + minRepeats 版
[[nodiscard]] static bool locateByMinRepeatAnchor(const Prefilter &pf, Input &input) {
  const auto &sa = pf.suffixAnchor;
  const char *cur = input.getIter();
  const char *end = input.getEnd();
  for (auto &lit : sa.literals) {
    const char *p = cur;
    while (p < end) {
      const StringRef haystack(p, static_cast<size_t>(end - p));
      auto retPos = haystack.find(StringRef(lit));
      if (retPos == StringRef::npos) {
        break;
      }
      const char *t = p + retPos;
      // 行頭まで遡る
      const char *lineStart = findLineStart(t, input.getBegin());
      // lineStart..t の区間にカンマが minRepeats 個以上あるか
      if (countByte(lineStart, t, ',') >= sa.minRepeats) {
        input.setIter(std::max(cur, lineStart));
        return true;
      }
      p = t + 1; // この出現は無効。次の出現へ
    }
  }
  return false; // 候補なし -> 即 FAIL
}
```

この入力（`3200.txt`）について実測すると、`z` は 7,059 回出現しますが、**13 個のカンマより後ろに `z` がある行は 1 行も存在しません**（全 302,278 行で 0 行）。したがって候補区間の和集合は空になり、VM を一度も走らせずに `FAIL` します。`(.*?,){13}z` の 13,305ms はほぼ丸ごと消える計算です（8.5 の候補数の実測を参照）。

### 8.5 期待効果

| パターン | 現状 arsh | 後置アンカー | 候補開始位置数（この入力、実測） |
|---|---:|---|---:|
| `.{0,2}(Tom\|Sawyer\|Huckleberry\|Finn)` | 2,002.6ms | `{Tom,Sawyer,Huckleberry,Finn}`、`maxPrefix=2` | **6,923**（全位置 16,013,977 の 0.043%） |
| `.{2,4}(Tom\|Sawyer\|Huckleberry\|Finn)` | 2,199.5ms | 同上、`maxPrefix=4` | **10,995**（0.069%） |
| `(.*?,){13}z` | 13,305.2ms | `z`、`minRepeats=13` | **0**（候補行なし） |

（候補数は、各必須リテラルの全出現 `[a, b)` について `[a - maxPrefixBytes, a]` を列挙し、`sameLine` 制約で絞った集合の実測値です。`{2,4}` の `maxPrefix=4` は「4 コードポイント × 最大4バイト = 16 バイト」を保守的に見積もったもので、実際の候補はさらに少なくなります。）

`.{0,2}(…)` 系は、改善案1の先頭集合では絞れない（全コードポイント）一方、後置アンカーでは候補が入力全体の 0.05% 未満になり、**VM 実行回数そのもの**が劇的に減ります。

### 8.6 正しさのガード

1. **アンカーは必須リテラルに限る**。`Alt` なら全分岐に共通のものではなく、**分岐ごとの**リテラルを候補和集合として扱う（`Tom|Sawyer|…` に共通リテラルは無いが、各分岐のリテラルは必須）。
2. **窓は保守的に広く取る**。`maxPrefixBytes` は「コードポイント数 × 4」で見積もり、必ず実際以上にする（狭く取るとマッチを取りこぼす）。
3. **開始位置を後ろへ飛ばさない**。`locateBySuffixAnchor` は常に `std::max(cur, winLo)` を返す。現在位置より前の候補でマッチする可能性を捨てない。
4. **`sameLine` は `AnyExceptNL` のときだけ**。`DOT_ALL`（`s` 修飾）では `.` が改行を跨ぐため `sameLine=false` にする。
5. **min-repeat は下限のみ**。カンマ数が `minRepeats` 未満の行は候補ゼロで正しい（必要十分ではないが**必要条件**なので安全）。
6. **`find` の探索開始位置**。窓が現在位置を含み得る最も早い位置（`cur - maxPrefixBytes`）から探す。`maxPrefixBytes == UINT32_MAX` のときは入力先頭から。
7. **リテラルは UTF-8 バイト列**。`memmem` は UTF-8 の自己同期性によりコードポイント途中に一致しない。

---

## 9. 改善案6: Unicode プロパティ名マップの遅延生成／完全ハッシュ

### 9.1 現状の計測

arsh の `mem_inst` 合計は 7,288B と小さい（全 6 エンジン中 2 番目。最小は quickjs の 2,512B）ですが、`\p{Sm}` の行だけは **4,688B** と突出しています（`result.csv` の該当行）。

```text
\p{Sm}; ... ; ... ;   79.3;    1.2; ... ; ... ; ...; ...; 4688; 2448; 696; 2504; ...
                                                    ^^^ \p{Sm} の arsh mem_inst (B)
```

この内訳は、`\p{...}` の初回コンパイル時に構築される**プロパティ名マップ**です。`parseCategory` / `parseScript` / `parseProperty` はいずれも関数ローカルの `static` で遅延生成されますが、`std::unordered_map`（`StrRefMap`）なのでヒープを確保します。

実測値（`tools/regex-bench/memory_tracker.cpp` と同じ計測器、`u` モード）:

| マップ | サイズ | 生成条件 |
|---|---:|---|
| 一般カテゴリ名（`categoryNames`、`Sm` など） | 4,216 B | 最初の `\p{...}`（一般カテゴリ） |
| スクリプト名（`scriptNames`、`Latin` など） | 18,288 B | 最初の `\p{Script=...}` |
| Lone プロパティ名（`Alphabetic` など） | 5,280 B | 最初の `\p{Alphabetic}` |
| 絵文字名（`Basic_Emoji` など） | 384 B | 最初の `\p{Basic_Emoji}` |
| **合計** | **28,168 B** | — |

`\p{Sm}` は一般カテゴリなので、その 4,688B のうち **4,216B（約 90%）がこの一般カテゴリ名マップ**で、残りの約 470B がコンパイル済みインスタンス本体です。つまりこの行の数値は、ほぼ**プロセス全体で共有される名前マップ**を、それを最初に生成したパターンの `mem_inst` に計上したものです。

しかも `tools/regex-bench` の計測は「実行全体を通じた live バイトの差分」なので、**マップを最初に生成したパターンがそのコストを全部背負います**（`tools/regex-bench/README.md` に明記されている既知の性質）。18 パターン中 `\p{...}` は `\p{Sm}` だけなので、この数値は実質的に計測アーティファクトでもあります。

### 9.2 改善方針

マップを**ヒープに置かない**ことで、計測上の偏りと実メモリの両方を同時に解消できます。`std::unordered_map` の代わりに「ソート済みの `static` 配列 + 二分探索」を使います。

- 関数ローカルの `static std::array<Entry, N>` は **.bss（静的領域）** に置かれ、ヒープには計上されません。構築中の一時 `std::vector` は直後に解放されるため live デルタに残りません。
- エントリ数は `std::size(categoryNames)` 等から**コンパイル時に決まります**（`categoryNames` は配列なので `constexpr`）。
- 参照局所性が良く、`unordered_map` よりキャッシュ効率も向上します。

### 9.3 `parseCategory` の書き換え（他も同型）

```cpp
// property.cpp

/** 名前 → 値 のソート済み静的テーブル。ヒープを使わない。 */
template <typename T, unsigned int N>
class NameTable {
private:
  struct Entry {
    StringRef name;
    T value;
  };

  // .bss に置かれる。ヒープには計上されない。
  std::array<Entry, N> entries{};
  unsigned int size_{0};

public:
  /** names[i] は '|' 区切りの別名。value は i に対応する。 */
  void init(const char *const (&names)[N]) {
    // 一時バッファ（この関数を抜けると解放される）で組み立ててからコピー
    std::vector<Entry> tmp;
    tmp.reserve(N);
    for (unsigned int i = 0; i < N; i++) {
      const auto value = static_cast<T>(i);
      splitByDelim(names[i], '|', [&tmp, value](StringRef ref, bool) {
        tmp.push_back({ref, value});
        return true;
      });
    }
    std::sort(tmp.begin(), tmp.end(),
              [](const Entry &x, const Entry &y) { return x.name < y.name; });
    assert(tmp.size() <= N); // 別名を含めても N 以下に収まるよう N を決める
    std::copy(tmp.begin(), tmp.end(), this->entries.begin());
    this->size_ = static_cast<unsigned int>(tmp.size());
  }

  Optional<T> find(const StringRef ref) const {
    auto begin = this->entries.begin();
    auto end = begin + this->size_;
    auto iter =
        std::lower_bound(begin, end, ref, [](const Entry &e, StringRef r) { return e.name < r; });
    if (iter != end && iter->name == ref) {
      return iter->value;
    }
    return {};
  }
};

static Optional<Category> parseCategory(const StringRef ref) {
  // N は別名込みの総数。生成スクリプトに上限を出力させると確実。
  static NameTable<Category, 64> table; // .bss（ヒープに計上されない）
  static const bool initialized = [] {
    table.init(categoryNames);
    return true;
  }();
  (void)initialized;
  return table.find(ref);
}
```

同様に `parseScript`（`NameTable<Script, ...>`）、`parseLone` / `parseProperty` / `parseEmojiProperty`（`NameTable<Lone, ...>` / `NameTable<RGIEmojiSeq, ...>`）を置き換えます。

> `std::array` のサイズは「別名を含む総数」を静的に上回る必要があります。`scripts` は `Zyyy|Common` のように 2 つ持つものがあるため、生成スクリプト（`scripts/unicode/gen_script_table.arsh` など）に「総別名数の上限」を出力させると確実です（現状は 177 スクリプト + 一部別名、38 一般カテゴリ、127 Lone 名）。

### 9.4 完全ハッシュ版（発展）

さらに初期化すら不要にしたい場合は、生成スクリプトに**ソート済みテーブルを直接出力**させます。`PropertyValueAliases.txt` は静的なので、コンパイル時に決まる `constexpr` 配列になり、実行時初期化も一時バッファも不要です。

```cpp
// 生成側（gen_*.arsh）が出力する形
static constexpr struct {
  const char *name;
  Category value;
} sortedCategoryNames[] = {
    {"C",  Category::C},   {"Cc", Category::Cc}, {"Cf", Category::Cf},
    {"Cn", Category::Cn},  /* ... name 昇順 ... */
};
```

この場合 `parseCategory` は `constexpr` 配列への `std::lower_bound` 1 回だけになります。

### 9.5 期待効果と限界

- **`\p{Sm}` の `mem_inst`**: 4,688B → 約 470B（コンパイル済みインスタンス本体のみ）。arsh の `mem_inst` 合計も 7,288B → 約 3KB に下がります。
- **`[sp]`（スコア）**: この施策自体はスコアに影響しません。ただし他の 17 パターンは元から小さいため、合計スコアの見え方は変わりません。
- **`mem_run`** は元から無関係（コンパイル時のコスト）です。
- **時間**: 二分探索は `unordered_map` と同等以上（要素数が少なくキャッシュに乗るため、多くの場合わずかに速い）。初回構築も一時バッファだけなので同等以下です。
- **限界**: この改善は 6 項目中もっとも効果が小さく（`\p{...}` 単独行の `mem_inst` のみ）、`result.csv` の合計値に対する寄与も小さい。優先度は低いですが、実装は独立しており、計測の見え方を公平にする意味があります。

---

## 10. 実装ポイント（該当箇所）

| 施策                      | 主なファイル / 行                                         |
|---------------------------|-----------------------------------------------------------|
| 先頭検索 fast path        | `src/regex/vm.cpp:311-379`（`searchLeadingBytes`）        |
| 再試行（1コードポイント） | `src/regex/vm.cpp:899-908`                                |
| リテラル融合              | `src/regex/emit.cpp:413-468`                              |
| Alt 生成                  | `src/regex/emit.cpp:470-489`                              |
| 文字列集合/radix 生成     | `src/regex/emit.cpp:775-860`                              |
| Matcher (String\/Radix)   | `src/regex/matcher.h:123-134,182-186`                     |
| Regex メタデータ追加      | `src/regex/regex.h:33-62`                                 |
| 文字列検索 (memmem)       | `src/misc/string_ref.hpp:129-138,188-192`                 |
| CodePointSet / Builder    | `src/misc/codepoint_set.hpp`, `src/unicode/set_builder.h` |
| ビルド対象追加            | `CMakeLists.txt:259-266`（新規 `analyze.cpp` を追加）     |
| アンカー検出（#3）        | `src/regex/parser.cpp`（`^`）、`src/regex/vm.cpp:478-492`（`Start`/`End`） |
| 行頭判定（#3）            | `src/regex/matcher.h:27-29`（`isLineTerminator`）、`src/regex/input.h:156-166` |
| radix 木（#4）            | `src/unicode/radix_tree.h:100-143`（`findLongestMatched`） |
| 後置アンカー（#5）        | `src/regex/vm.cpp:229-259`（`findLongestMatched`）        |
| プロパティ名マップ（#6）  | `src/unicode/property.cpp:52-70,192-202,310-337,651-668,737-748` |

---

## 11. テスト方針

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

## 12. パターン別の期待効果

| パターン                         |     現状 arsh | 効くフィルタ                                        | 期待                |
|----------------------------------|--------------:|-----------------------------------------------------|---------------------|
| `[a-z]shing`                     |       159.7ms | leading `{a-z}` + literal `"shing"`                 | 桁改善（〜10ms 級） |
| `\b\w+nn\b`                      |       379.1ms | leading 語文字 + literal `"nn"`                     | 桁改善              |
| `[a-zA-Z]+ing`                   |       752.6ms | leading 英字 + literal `"ing"`                      | 桁改善              |
| `\s[a-zA-Z]{0,12}ing\s`          |       260.5ms | literal `"ing"`（+ leading `\s`）                   | 改善                |
| `Huck…\|Saw…`                    |       385.4ms | leading `{H,S}`                                     | 大きく改善          |
| `Tom\|Sawyer\|Huckleberry\|Finn` |       681.3ms | leading `{T,S,H,F}`                                 | 大きく改善          |
| `(?i)Tom\|Sawyer\|…`             |       652.3ms | leading 逆像                                        | 改善                |
| `(?i)Twain`                      |       185.2ms | leading 逆像 `{T,t}`（IChar consume）               | 大きく改善          |
| `∞\|✓`                          |       330.5ms | leading `{∞,✓}`（非ASCII走査）                     | 大きく改善          |
| `([A-Za-z]awyer\|…)\s`           |       425.2ms | BeginCapture → locate `{A-Za-z}`                    | 大きく改善          |
| `Tom.{10,25}river\|…`            |       393.4ms | Alt → locate `{T,r}`（#2）、radix（#4）            | 改善                |
| `(.*?,){13}z`                    |     13305.2ms | 後置アンカー `"z"` + min-repeat（#5）            | ほぼ 0ms 級         |
| `.{0,2}(Tom\|…)` / `.{2,4}(…)`   | 2002 / 2199ms | 後置アンカー `{Tom,Sawyer,…}`（#5）                | 桁改善              |

注意点: `[a-z]shing` は現状でも `CharSet` consume が効いており、遅さの主因は「各小文字位置で `String("shing")` を試す」ことなので、**#1 の必須リテラル**が効きます。`(.*?,){13}z` は先頭が nullable で locate が無効、必須リテラルも弱選択（`,`）になるため **#5** が必要です。

### #3〜#6 の対象パターン

上位 6 項目のうち、#3〜#6 が直接効くのは以下です（それ以外は #1/#2 の領分）。

| # | パターン | 現状 arsh | 効くフィルタ | 期待 |
|---|---|---:|---|---|
| 3 | 先頭 `^`（非 multiline） | — | 再試行 1 回化 | 再試行ぶんの削減 |
| 3 | 先頭 `^`（multiline） | — | 行頭ジャンプ | 試行位置の削減 |
| 4 | `Huck…\|Saw…` | 385.4ms | 分岐リテラル radix `{Huck,Saw}` | 大きく改善 |
| 4 | `Tom\|Sawyer\|Huckleberry\|Finn` | 681.3ms | 分岐リテラル radix（4 分岐） | 大きく改善 |
| 4 | `(?i)Tom\|Sawyer\|…` | 652.3ms | fold 済みキーの radix | 大きく改善 |
| 5 | `.{0,2}(Tom\|…)` / `.{2,4}(…)` | 2002 / 2199ms | 後置アンカー（有界プレフィックス） | 桁改善 |
| 5 | `(.*?,){13}z` | 13305.2ms | 後置アンカー `"z"` + min-repeat | ほぼ 0ms 級 |
| 6 | `\p{Sm}` | 79.3ms | 名前マップの非ヒープ化 | `mem_inst` 4,688B → 約 470B |

---

## 13. 留意点・既知の限界

- `.{0,2}(Tom|Sawyer|Huckleberry|Finn)` のように「可変長の任意消費が先頭にあり、その後が `Alt`」のケースは、先頭集合＝全文字・共通リテラル無しになるため #1/#2 では改善しません。#5（先頭 `.*` の後方にある必須リテラルの位置を基準に候補位置を決める後置アンカー探索、第8章）で対処します。
- `Alt` の全分岐がリテラルの場合、locate-only よりさらに速い「分岐リテラル radix で直接ジャンプ」は #4（第7章）の領分です。
- ignore-case の逆像 fold 索引（約9MB）を避けたい場合は、`IGNORE_CASE` の先頭 `Char` に限り leading フィルタを無効化する保守的フォールバックも選べます（`(?i)Twain` の改善は失われますが安全）。
- **#3 の `\A` は arsh に存在しません**。`u`/`v` モードの `\A` はエラー、`BMP` モードでは文字 `A` として扱われるため、「アンカー」は非 multiline の `^` のみが対象です。
- **#4 は prefix-free なリテラル分岐に限定**。`ab|a` のようにキーが接頭辞関係にある場合は適用できず、既存の `Alt` 実行にフォールバックします（第7章 7.6）。
- **#5 の効果は入力に大きく依存**。`.{0,2}(…)` は必須リテラルの出現数だけ候補が残る一方、リテラルが高頻度だと候補削減が効きません。`(.*?,){13}z` はこの入力では候補ゼロですが、一般には min-repeat 条件を満たす行だけが候補になります。
- **#6 は計測アーティファクトの是正が主目的**。実メモリ削減は約 28KB（全名前マップ合計）で、18 パターン合計の `[sp]` には影響しません。
- 本ドキュメントは分析・設計提案であり、コード変更は行っていません。
