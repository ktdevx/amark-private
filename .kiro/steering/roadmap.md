# Roadmap

## Overview
amark は、Markdown を構文木として表し、原文に忠実に解析する C99 のライブラリである。Markdown を扱うソフトが、CommonMark に完全に準拠した解析結果を、原文の範囲（オフセットと行番号）として受け取れるようにする。ノードは原文を複製せず、範囲から原文を完全に復元できる。方言（GFM など）は本体を変えずに差し込める。

方式は「ブロック解析 → インライン解析」の 2 段階とする。ノードはアリーナ上の平坦な配列に置き、親子関係は添字で表す。処理はすべて明示的なスタックで行い、再帰を使わない。方言は決まった差し込み口に規則を登録し、規則が返した結果は本体が検査する。

## Approach Decision
- **Chosen**: 2 段階の解析と平坦な木。ブロックを解析してからインラインを解析する。ノードは配列と添字で表し、再帰は使わない。方言は決まった差し込み口（ブロックの開始と継続、インラインの起動文字、後処理）に規則として登録する。Unicode の表は、生成スクリプトで自前に作る。
- **Why**: 求められている性質を素直に満たせる。原文を複製しない、範囲の不変条件、再帰を使わない、状態を持たない、失敗したら全部解放する、の各点である。CommonMark の参照実装（cmark）と同じ段階構成なので、spec.txt に準拠しやすい。Unicode の表を自前で作れば、外部依存がなく、確保関数の差し替えやグローバル状態の問題も起きない。表も小さい（句読点は約 3 KB、case folding は数十 KB 以下）。
- **Rejected alternatives**:
  - md4c 型のイベント駆動の本体と、その上で木を組む層：参照の解決、方言の結果の検査、範囲の不変条件の扱いが複雑になる。
  - cmark などのフォーク：原文を複製し、木の構造も要件と違うため、改造量が多い。
  - utf8proc：MIT で保守も活発だが、確保関数を差し替えられず、約 350 KB と大きい。
  - ICU：必要な表は 3 つだけなのに、データだけで約 30 MB あり、C++ のツールチェーンも要る。

## Scope
- **In**:
  - データモデル：CommonMark の全要素と、方言が定める種類のノード。範囲と行番号、種類ごとの属性、不変条件、読み取り専用でたどる API、文書の解放。
  - ブロックの解析とインラインの解析。CommonMark 0.31.2 に準拠する。
  - 原文の文字列の取り出し：論理的な文字列を組み立て、エスケープと文字参照を文字に戻し、dest・title・info の意味上の値を得る。
  - 方言の拡張点と、規則が返した結果の検査、診断。
  - 任意設定：方言、解析する範囲 `[start, end)`、確保関数、診断に付けるファイル名、不正な UTF-8 の扱い（エラーか U+FFFD への置換か）、資源の上限（入力の大きさ、入れ子の深さ、ノードの数）。
  - 内部の部品：アリーナ、可変長の配列、行の索引、UTF-8 の検査。Unicode の表と HTML5 の実体名の表は、生成スクリプトで作る。
  - テスト専用の HTML 出力、spec.txt の準拠テスト、病的な入力のテスト、原文の復元のテスト。
  - ビルドと CI：CMake、静的と動的のライブラリ、Windows・Linux・macOS。
- **Out**:
  - 公開する HTML 出力。
  - 木から Markdown を書き出す処理と、範囲を指定して原文を差し替える補助 API。後の段階の候補とする（「Future Phases」を参照）。
  - I/O と BOM の除去、フロントマターなど本文の外側の解析。BOM とフロントマターは、利用者が解析する範囲の指定で除く。
  - 用途に固有の処理（翻訳の単位への分割、原文の置き換えなど）。
  - GFM の各拡張（表、取り消し線、タスクリスト、脚注、拡張オートリンク）の実装。拡張点が固まった後の段階で、別の spec にする。
  - 構文の規則を設定で変えること（タブ幅 4、ラベルの上限 999 文字などは固定）。

## Constraints
- C99。コンパイラの拡張には頼らない。
- CMake。テストのビルドには、Naos の要件で 3.21 以上が要る。
- 静的ライブラリと動的ライブラリの両方を作る。シンボルの公開マクロを用意する。
- 対象は Windows（MSVC と MinGW）、Linux、macOS。CI は GitHub Actions でこの 3 つの OS を回す。
- ライセンスは MIT、著作権表示は「The Amark Authors」。Unicode の表のデータには、Unicode License v3 の表示を付ける。
- テストフレームワークは Naos（https://github.com/vabelorg/naos）。1.0 前なので、`v0.4.0` のタグかコミットに固定して FetchContent で取り込む。
- 準拠の対象は CommonMark 0.31.2。句読点は P と S、空白は Zs とタブ・LF・FF・CR、case folding は C と F を使う。
- 性質（どの設定でも保証する）：
  - どの入力も文書になる。失敗するのは、入力の制約違反、設定の誤り、資源の上限を超えたとき、メモリ不足のときだけ。
  - spec.txt をすべて通る。
  - 原文を失わない。行末記号も入力のまま保つ。
  - 時間は入力の長さにほぼ比例し、再帰を使わない。
  - グローバルな状態も I/O もなく、結果は決定的で、別々の文書を並行して解析できる。
  - 失敗が局所的。失敗したときは全部解放し、部分的な木を返さない。

## Boundary Strategy
- **Why this split**:
  - 土台（project-foundation、unicode-tables）、データモデル（syntax-tree）、解析の 2 段階（block-parser、inline-parser）、原文の読み出し（source-text）、拡張（dialect-extension）、総合の検証（spec-conformance）を分けた。各 spec の責任が 1 つになり、レビューする範囲も狭く保てる。
  - unicode-tables と syntax-tree、inline-parser と source-text は、それぞれ並行して進められる。
- **Shared seams to watch**:
  - **syntax-tree ⇔ block-parser / inline-parser**：ノードを追加する内部 API、範囲の不変条件、属性の格納方法。解析器は内部 API だけで木を作り、不変条件を検査できる形で残す。
  - **block-parser ⇔ source-text**：コンテナのプレフィックスやタブの残りの桁など、行ごとの情報をどう記録するか。source-text は、この記録から論理的な文字列を組み立てる。
  - **block-parser ⇔ inline-parser**：リンク参照定義の表と、ラベルの正規化を共有する。
  - **block-parser / inline-parser ⇔ dialect-extension**：差し込み口の位置と呼び出しの契約。解析器の設計の段階で差し込み口を確保し、dialect-extension で公開と検査を仕上げる。
  - **syntax-tree の公開する設定 ⇔ 各解析器**：範囲、UTF-8 の扱い、資源の上限、確保関数を、どこで検査し、どこで適用するか。
  - **unicode-tables ⇔ 利用する側**：参照する関数の形（コードポイントの判定と、畳み込み結果の書き出し）。

## Future Phases
今回の 8 つの spec が終わり、拡張点と公開 API が固まった後の候補である。今回の spec では作らない。ただし、ここに挙げたものを後から本体の設計を変えずに足せることは、syntax-tree と source-text の設計で確かめておく。下の「Specs (dependency order)」には含めない（`/kiro-spec-batch` の対象外）。

- **markdown-writer**：木から Markdown を書き出す。書式は正規化する（cmark の `cmark_render_commonmark` に相当）。
  - 用途：整形ツール、プログラムで組み立てた文書の出力、「書き出す → もう一度解析する → 同じ木になる」の往復テスト。
  - 決めること：木を作る・書き換える公開 API が要るか。エスケープと正規化の方針。
  - Dependencies: syntax-tree, source-text
- **source-edit**：範囲を指定して原文を差し替える補助 API。変えない部分は原文のまま残す（書式も行末記号も変わらない）。
  - 用途：翻訳した文章での置き換えなど、原文に忠実な編集。
  - Dependencies: syntax-tree, source-text
- **GFM の各拡張**：表、取り消し線、タスクリスト、脚注、拡張オートリンク。拡張ごとに 1 つの spec にする。
  - Dependencies: dialect-extension, spec-conformance

## Specs (dependency order)
- [ ] project-foundation -- CMake・CI・ライセンス・Naos の統合と、内部の部品（確保関数、アリーナ、配列、行の索引、UTF-8 の検査と置換）。Dependencies: none
- [ ] unicode-tables -- UCD と entities.json から C の表を作る生成スクリプトと、参照する関数（句読点 P+S、空白、case folding C+F、HTML5 の実体名）。Dependencies: project-foundation
- [ ] syntax-tree -- 構文木のデータモデル、種類ごとの属性、不変条件とその検査、読み取り専用でたどる API、公開する設定・エラー・診断の型。Dependencies: project-foundation
- [ ] block-parser -- 解析の入口と設定の適用、ブロックの解析、リンク参照定義（ラベルの正規化と定義の表）。Dependencies: syntax-tree, unicode-tables
- [ ] inline-parser -- インラインの解析（コードスパン、エスケープ、文字参照、リンクと画像と参照の解決、オートリンク、生の HTML、強調、改行）。Dependencies: block-parser
- [ ] source-text -- ノードの範囲から論理的な文字列を組み立て、dest・title・info の意味上の値を得る。Dependencies: block-parser, unicode-tables
- [ ] dialect-extension -- 方言の差し込み口、規則が返した結果の検査と診断、テスト用の方言。Dependencies: block-parser, inline-parser
- [ ] spec-conformance -- テスト専用の HTML 出力、spec.txt 0.31.2 の準拠テスト、病的な入力・原文の復元・並行性のテスト。Dependencies: inline-parser, source-text
