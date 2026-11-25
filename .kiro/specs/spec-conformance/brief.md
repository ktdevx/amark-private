# Brief: spec-conformance

## Problem
ライブラリの約束（spec.txt に完全に準拠する、原文を失わない、病的な入力に強い、状態を持たない、失敗が局所的）を、全体を通して確かめる仕組みがない。spec.txt は「入力 → 期待する HTML」の形式なので、比べるには木を HTML にする処理が要る。

## Current State
inline-parser と source-text ができてから着手する。各 spec の単体テストは、それぞれの spec で書く。

## Desired Outcome
- テスト専用の HTML 出力がある。公開 API にはしない。source-text を使い、CommonMark の参照実装と同じ HTML を作る（URL の正規化と HTML のエスケープを含む）。
- spec.txt（CommonMark 0.31.2）のすべての例を取り込み、Naos のテストとして実行する。全件が通る。HTML の比べ方は、参照実装の正規化に合わせる。
- 病的な入力のテストがある。深い入れ子、大量の区切り、閉じない括弧など、cmark の pathological tests 相当を使う。
  - 時間がほぼ線形であることを確かめる。
  - スタックがあふれないことを確かめる。
- 原文の復元のテストがある。どの入力でも、範囲から原文を完全に復元でき、行末記号も保たれることを確かめる。
- 不変条件のテストがある。どの入力でも、syntax-tree の不変条件の検査を通ることを確かめる。
- 失敗のテストがある。確保の失敗を注入し、すべて解放されること、部分的な木が返らないことを確かめる。資源の上限、不正な UTF-8 の扱い（エラーと置換）も確かめる。
- 決定性と並行性のテストがある。同じ入力からは同じ木ができること、別々の文書を並行して解析できることを確かめる。
- これらのテストを CI の 3 つの OS で回す。サニタイザ付きの構成も含む。

## Approach
spec.txt から例を取り出すスクリプトで、テストのデータを生成する（生成物はコミットする）。HTML 出力は tests/ 配下に置き、ライブラリには含めない。確保の失敗の注入には、設定の確保関数を差し替える。

## Scope
- **In**:
  - テスト専用の HTML 出力
  - spec.txt の取り込みと実行
  - 病的な入力・原文の復元・不変条件・失敗・決定性・並行性のテスト
  - CI への組み込み
- **Out**:
  - 公開する HTML 出力（範囲外）
  - GFM の spec のテスト（後の段階）
  - ファジング（必要になったら追加する。テストの入口は再利用できる形にしておく）

## Boundary Candidates
- テスト用の HTML 出力
- spec.txt の実行
- 性質のテスト（病的な入力、原文の復元、不変条件、失敗、並行性）

## Out of Boundary
- 各解析器の単体テスト（各 spec が持つ）

## Upstream / Downstream
- **Upstream**: inline-parser、source-text、block-parser、syntax-tree、project-foundation（Naos と CI）
- **Downstream**: 後の段階の GFM の spec（テストの基盤を再利用する）

## Existing Spec Touchpoints
- **Extends**: なし
- **Adjacent**: dialect-extension（方言のテストは dialect-extension が持つ。ただし、スレッドを使う並行性のテストは、この spec が、dialect-extension のテスト用の方言（`tests/support/test_dialect.h`）を指定した解析を含めて行う）

## Constraints
- HTML 出力はテスト専用で、インストールされるライブラリに含めない。
- spec.txt の版は 0.31.2 に固定する。
