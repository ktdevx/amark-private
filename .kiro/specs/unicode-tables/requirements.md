# Requirements Document

## Project Description (Input)
### Problem
CommonMark 0.31.2 は、強調の flanking の判定に Unicode の句読点（General Category P と S）と空白（Zs とタブ・LF・FF・CR）を使う。リンクのラベルの正規化には full case folding（CaseFolding.txt の C と F）を、文字参照の解決には HTML5 の実体名の表（entities.json）を使う。外部の部品（utf8proc、ICU）は、確保関数を差し替えられなかったり、大きすぎたりする。

### Current State
表も生成の仕組みもない。調べたところ、cmark と md4c は、どちらも UCD から C の表を生成するスクリプトを持っている。

### Desired Outcome
- 生成スクリプトがある。入力は UCD（DerivedGeneralCategory.txt、CaseFolding.txt）と entities.json で、出力は C のソース。スクリプトと入力の版はリポジトリで管理する。
- 次の関数がある。どれも純粋な関数で、グローバルな状態を持たない。
  - 句読点（P+S）と空白を判定する。範囲の表を二分探索する。
  - case folding。C+F に従い、1 文字を最大 3 コードポイントに展開する。書き出し先のバッファは呼び出す側が渡し、確保はしない。
  - 実体名を引く。名前から UTF-8 の列を返す。
- Unicode の版を上げるときは、表を再生成するだけで済む。生成した結果が最新かどうかを CI で確かめられる。
- Unicode License v3 の表示を、ライセンスのファイルに含める。

### Approach
md4c と cmark にならい、Python などのスクリプトで、範囲の表とソート済みの対応表を生成する。生成物はリポジトリにコミットし、ビルド時には生成しない。こうすると、ビルドに Python は要らない。

### Scope
- **In**:
  - 生成スクリプト、入力データ、生成した表、参照する関数、単体テスト
  - spec の例を含め、ẞ → ss のような full folding のケース
- **Out**:
  - ラベルの正規化そのもの。前後の空白の除去と、連続する空白の畳み込みは block-parser が行う。
  - 正規化（NFC など）、書記素の分割、文字幅

### Boundary Candidates
- 生成の仕組み（スクリプトと入力データ）
- 実行時に参照する関数

### Out of Boundary
- UTF-8 の検査とデコード（project-foundation）
- 解析の規則での使い方（block-parser、inline-parser、source-text）

### Upstream / Downstream
- **Upstream**: project-foundation、Unicode 18.0.0 の UCD、WHATWG の entities.json
- **Downstream**: block-parser（ラベルの正規化）、inline-parser（flanking、文字参照）、source-text（文字参照を文字に戻す）

### Existing Spec Touchpoints
- **Extends**: なし
- **Adjacent**: project-foundation（UTF-8 のデコード）

### Constraints
- 表は小さく保つ。句読点の範囲は約 360 個、case folding は約 1,600 項目、実体名は約 2,200 個。
- C99 の const な表だけを使い、初期化の処理を要しない。

## Requirements
<!-- Will be generated in /kiro-spec-requirements phase -->


