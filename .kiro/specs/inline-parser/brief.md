# Brief: inline-parser

## Problem
段落や見出しの内容に含まれるインライン構造を、CommonMark 0.31.2 の規則どおりに解析する部分が要る。強調の区切りの扱いや、リンクの括弧の対応は、素朴に書くと時間が入力の 2 乗になったり、再帰が深くなったりしやすい。

## Current State
block-parser ができてから着手する。block-parser は、内容の範囲とリンク参照定義の表を渡す。

## Desired Outcome
- 次の要素を解析し、ノードとして木に加える。
  - コードスパン：バッククォートの列の長さが一致するものを探す。探索は線形の時間で行う。
  - バックスラッシュのエスケープ
  - 文字参照：実体名、10 進、16 進
  - リンクと画像：インライン、完全参照、省略参照、ショートカット。参照は定義の表で解決する。dest と title の範囲、参照先の定義を属性として持つ。
  - オートリンク：URI とメールアドレス
  - 生の HTML：開始タグ、終了タグ、コメント、処理命令、宣言、CDATA
  - 強調と強い強調：区切りのスタックの方式と、3 の倍数の規則。句読点と空白の判定には unicode-tables を使う。
  - ハードな改行とソフトな改行
  - テキスト
- 病的な入力（ネストした括弧、大量の区切り、閉じないコードスパンなど）でも、時間がほぼ線形で、再帰を使わない。
- 方言の差し込み口（インラインの起動文字、区切りの種類）を、設計の段階で確保しておく。

## Approach
cmark の区切りのスタックと括弧のスタックの方式に従い、`openers_bottom` などの下限の表で、時間を線形に保つ。コードスパンは、バッククォートの列の長さごとに、最後に見た位置を記録して線形にする。ノードは syntax-tree の内部 API で作り、範囲だけを持つ。

## Scope
- **In**:
  - 上に挙げたインラインの要素すべて
  - 参照の解決
  - 線形の時間を保つ工夫
  - インラインの単体テスト
- **Out**:
  - エスケープと文字参照を文字に戻した値を作ること（source-text）
  - 方言の公開 API（dialect-extension）
  - GFM の取り消し線や拡張オートリンク（後の段階）

## Boundary Candidates
- 字句の走査（起動文字の表）
- 区切りの処理（強調）
- 括弧の処理（リンクと画像、参照の解決）

## Out of Boundary
- URL の正規化やパーセントエンコード（出力する側の責任。テスト用の HTML 出力では spec-conformance が扱う）

## Upstream / Downstream
- **Upstream**: block-parser（内容の範囲、定義の表、ラベルの正規化）、syntax-tree、unicode-tables
- **Downstream**: dialect-extension、spec-conformance

## Existing Spec Touchpoints
- **Extends**: なし
- **Adjacent**: source-text（同じ範囲を読む）、dialect-extension

## Constraints
- 再帰を使わない。時間は入力の長さにほぼ比例する。
- 原文を複製しない。
