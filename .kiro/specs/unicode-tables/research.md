# Research & Design Decisions

## Summary
- **Feature**: `unicode-tables`
- **Discovery Scope**: New Feature（表も生成の仕組みもない状態からの新規作成。ビルド、テスト、検査は project-foundation のものに乗る）
- **Key Findings**:
  - Unicode 18.0.0 の UCD と entities.json を実際に取得して数えた。句読点（P+S）は 359 範囲、Zs は 7 範囲、case folding は状態 C が 1,501 項目、状態 F が 105 項目（2 文字が 89、3 文字が 16）、`;` で終わる実体名は 2,125 個（最長 31 文字、名前の合計 14,004 バイト、UTF-8 は最長 6 バイト）である。どれも単純な「ソート済みの表と二分探索」で足り、表の合計は約 52 KB になる。
  - 既存の検査 `checks.objects` は、可変の静的データ（`nm` の型 `d` など）を禁じている。ポインタを含む `const` の表は、位置独立コードでは再配置が要るので `.data.rel.ro` に置かれ、この検査に落ちる。表にはポインタを入れず、添字と長さだけを入れる。
  - entities.json の利用の条件は、WHATWG の HTML Standard の表示が定めている。文書としては CC BY 4.0、ソースコードに取り込んだ部分は BSD 3-Clause である。入力データの複製と生成した表とで、適用される条件が違う。

## Research Log

### 入力データの実物（Unicode 18.0.0 と entities.json）
- **Context**: 表の形と大きさ、生成スクリプトの検査の内容を決めるのに、実際の件数と書式が要る。
- **Sources Consulted**:
  - `https://www.unicode.org/Public/18.0.0/ucd/extracted/DerivedGeneralCategory.txt`（282,143 バイト、SHA-256 `d6b151d2…030f94`）
  - `https://www.unicode.org/Public/18.0.0/ucd/CaseFolding.txt`（88,776 バイト、SHA-256 `a0047976…587b0d`）
  - `https://html.spec.whatwg.org/entities.json`（145,897 バイト、SHA-256 `d741d877…f362e6`、2026-11-23 に取得）
  - 取得したファイルを、使い捨てのスクリプトで数えた。
- **Findings**:
  - 2 つの UCD のファイルは、1 行目が `# <名前>-18.0.0.txt` の形である。版はここから読める。
  - DerivedGeneralCategory.txt の行は `XXXX..YYYY ; Gc # 注釈` または `XXXX ; Gc # 注釈` である。載っていないコードポイントは Cn である。
  - P と S の範囲を合わせて、隣り合うものをつなぐと 359 範囲になる。うち 206 が BMP にある。最大は U+1FBFA である。
  - Zs は U+0020、U+00A0、U+1680、U+2000–U+200A、U+202F、U+205F、U+3000 の 7 範囲である。
  - CaseFolding.txt の行は `XXXX; 状態; 対応先; # 注釈` である。状態は C、F、S、T の 4 つである。C は 1,501 項目、F は 105 項目で、F の対応先は 2 つか 3 つである。U+1E9E には F（`0073 0073`）と S（`00DF`）の両方がある。
  - C の対応元の最大は U+1E921、対応先の最大は U+1E943 である。16 ビットには収まらない。
  - entities.json は 2,231 項目ある。`;` で終わる名前は 2,125 個で、残りの 106 個は `;` のない古い形である。古い形の名前には、すべて `;` のある形もある。
  - `;` で終わる名前は、すべて `&[A-Za-z0-9]+;` の形である。`&` と `;` を除いた長さは最大 31 である。コードポイントは 1 つか 2 つ（2 つのものは 93 個）で、UTF-8 にすると最大 6 バイトになる。
  - どのファイルも行末は LF で、CR を含まない。
- **Implications**:
  - 件数は brief の見積もり（約 360、約 1,600、約 2,200）とほぼ合う。実体名は、`;` のない形を除くので 2,125 個になる。
  - 生成スクリプトは、Unicode の版をファイルの 1 行目から読む。版を別の場所に書く必要はない。
  - entities.json には版がない。生成物の先頭には、ファイルの SHA-256 を「版」として書く。取得元と取得した日は `tools/data/README.md` に書く。

### 既存の検査との整合（checks.objects、checks.license_headers、checks.exports）
- **Context**: 生成した表と参照する関数は、project-foundation のリポジトリの検査を通らなければならない。
- **Sources Consulted**: `tests/checks/CheckObjects.cmake`、`tests/checks/CheckLicenseHeaders.cmake`、`tests/CMakeLists.txt`、`CONTRIBUTING.md`
- **Findings**:
  - `checks.objects` は、`nm` の型が `b B C d D g G s S` のシンボルを禁じ、`r R`（読み取り専用）を許す。
  - `static const char *const names[]` のようにポインタを持つ表は、位置独立コード（動的ライブラリ、PIE）では実行時の再配置が要る。GCC と Clang は、これを `.data.rel.ro` に置き、`nm` は `d` と表示する。
  - `checks.license_headers` は、`.c` と `.h` の先頭 5 行に、文字列 `Copyright (c) The Amark Authors` と `SPDX-License-Identifier: MIT` があることを、部分文字列の一致で確かめる。`SPDX-License-Identifier: MIT AND Unicode-3.0` はこの検査を通る。
  - `checks.license_headers` は `.txt`、`.json`、`.py`、`.md` を調べない。`tools/data/` の入力データに除外の設定を足す必要はない。
  - `checks.exports` は、動的ライブラリの公開シンボルを一覧と比べる。`AMARK_API` を付けない関数は公開されない。
- **Implications**:
  - 表にはポインタを入れない。実体名は、名前を 1 つにつないだ文字の配列と、位置と長さを持つ項目の配列で表す。
  - 生成物は `.h` にする。先頭の表示が検査の対象になり、生成スクリプトが表示を書き忘れると検査で分かる。
  - 生成物の SPDX の表記は、データの条件も示す複合の形（`MIT AND Unicode-3.0`、`MIT AND BSD-3-Clause`）にする。
  - 要件 8.4（表が読み取り専用）と 8.9（公開しない）は、新しい検査を作らなくても、既存の検査で確かめられる。

### 行末の変換とバイト単位の一致
- **Context**: 要件 1.2 と 3.1 は、生成物がどの OS でもバイト単位で同じであることを求める。
- **Sources Consulted**: `.github/workflows/ci.yml`、Git の `core.autocrlf` と `.gitattributes` の動作
- **Findings**:
  - リポジトリに `.gitattributes` はない。
  - Windows の Git は、既定で `core.autocrlf=true` である。チェックアウトのときに、テキストのファイルの行末を CRLF に変える。
  - 入力データの行末が変わると、SHA-256 が変わる。生成物の行末が変わると、作り直した結果と一致しなくなる。
- **Implications**:
  - `.gitattributes` を足す。入力データは変換しない（`-text`）。生成物は常に LF にする（`text eol=lf`）。
  - 生成スクリプトは、入力をバイト列として読み、出力を LF で書く。

### entities.json の利用の条件
- **Context**: 要件 9.2。brief は Unicode License v3 だけを挙げていた。
- **Sources Consulted**: `https://html.spec.whatwg.org/multipage/acknowledgements.html` の末尾、`https://github.com/whatwg/html` の `LICENSE`
- **Findings**:
  - 表示は次のとおりである。「Copyright © WHATWG (Apple, Google, Mozilla, Microsoft). This work is licensed under a Creative Commons Attribution 4.0 International License. To the extent portions of it are incorporated into source code, such portions in the source code are licensed under the BSD 3-Clause License instead.」
  - `whatwg/html` の `LICENSE` には、CC BY 4.0 の全文と BSD 3-Clause の全文がある。
- **Implications**:
  - `tools/data/entities.json`（複製）には CC BY 4.0 が適用される。出所、著作権表示、ライセンスへのリンクを書く。
  - `src/entity_table.h`（ソースコードに取り込んだ部分）には BSD 3-Clause が適用される。BSD 3-Clause の全文と著作権表示を `LICENSE` に書く。

### Unicode License v3
- **Context**: 要件 9.1。
- **Sources Consulted**: `https://www.unicode.org/license.txt`
- **Findings**:
  - 題は「UNICODE LICENSE V3」、著作権表示は「Copyright © 1991-2026 Unicode, Inc.」である。SPDX の識別子は `Unicode-3.0` である。
  - データファイルの複製と、データファイルを取り込んだソフトウェアの両方に、著作権表示と許諾の表示を付けることを求める。
- **Implications**: 全文を `LICENSE` に入れ、適用されるファイル（`tools/data/` の 2 つと `src/unicode_tables.h`）を書く。

### 先行する実装（cmark、md4c）
- **Context**: 表の形と生成の方法の参考にする。
- **Sources Consulted**: 知識にある cmark（`tools/make_entities_inc.py`、`src/entities.inc`、`src/case_fold.inc`）と md4c（`scripts/build_*.py`、`src/md4c.c` の表）の構成。今回、ソースは取得していない。
- **Findings**:
  - cmark は、Python で `entities.inc` を作り、ソートした表を二分探索する。case folding は、UTF-8 のバイト列どうしの対応を `switch` で持つ。
  - md4c は、Python で範囲の表を作り、二分探索する。case folding は、対応先の個数ごとに表を分ける。
- **Implications**: どちらも「スクリプトで生成し、生成物をコミットし、二分探索で引く」形である。この設計も同じ形をとる。case folding の表を個数で分ける点は、md4c にならう。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| ソート済みの表と二分探索 | 範囲や項目をコードポイント（または名前）の順に並べ、二分探索で引く | 単純。表が小さい（約 52 KB）。生成物を人が読める | 1 回の参照に 7〜12 回の比較が要る | 採用 |
| 2 段のトライ（ページの表） | コードポイントの上位で索引を引き、下位でビットを見る | 参照が一定の時間 | 表が大きくなる。生成が複雑になる。差分が読めない | 採らない。参照の回数は入力の長さに比例する程度で、二分探索で足りる |
| 完全ハッシュ（実体名） | gperf などで衝突のないハッシュ関数を作る | 参照が一定の時間 | 生成に別の道具が要る。生成物が読めない | 採らない |
| 外部の部品（utf8proc、ICU） | 既存のライブラリを使う | 実装が要らない | 確保関数を差し替えられない。大きい。実行時の依存が増える | steering で採らないと決めてある |

## Design Decisions

### Decision: 生成スクリプトは Python 3 の標準ライブラリだけで書く
- **Context**: 要件 1.1〜1.3、2.6。スクリプトの実行環境は、表を再生成する人と CI だけが要る。
- **Alternatives Considered**:
  1. Python 3（標準ライブラリだけ）
  2. CMake のスクリプト（`cmake -P`）
  3. C で書いた生成用のプログラム
- **Selected Approach**: Python 3.9 以上で動く 1 つのスクリプト `tools/gen_tables.py`。`json`、`hashlib`、`difflib`、`argparse` など標準ライブラリだけを使う。
- **Rationale**: JSON と 16 進の表の処理が短く書ける。cmark と md4c も Python を使っている。GitHub Actions のランナーには初めから入っている。CMake のスクリプトは JSON の扱い（3.19 以上の `string(JSON)`）が遅く、2,000 項目の処理に向かない。C のプログラムは、JSON の読み取りを自分で書くことになる。
- **Trade-offs**: 表の再生成に Python が要る。通常のビルドには要らない（要件 1.8）。
- **Follow-up**: Python の版による出力の違いが出ないことを、CI（Linux）と CTest（Python のある各 OS）の一致の検査で確かめる。

### Decision: 生成物は、1 つの `.c` だけが取り込む `.h` にする
- **Context**: 表を、参照する関数からどう見えるようにするか。
- **Alternatives Considered**:
  1. 生成物を `.c` にし、表を外部リンケージの `const` にして内部ヘッダで宣言する
  2. 生成物を `.h` にし、表を `static const` にして、参照する関数の `.c` だけが取り込む
  3. 生成物を `.inc` にする（cmark の形）
- **Selected Approach**: 2。`src/unicode_tables.h` を `src/unicode.c` が、`src/entity_table.h` を `src/entity.c` が取り込む。
- **Rationale**: 表は `static` なので、ライブラリのシンボルが増えず、ほかのモジュールから表に直接触れない。表の形を変えても、関数の契約が同じなら下流に影響しない。`.h` は `checks.license_headers` の対象なので、先頭の表示が検査される。`.inc` は検査の対象にならない。
- **Trade-offs**: データの定義をヘッダに置くことになる。取り込む `.c` が 1 つだけであることを、ファイルの先頭の注釈に書く。
- **Follow-up**: なし。

### Decision: 表にポインタを入れない
- **Context**: `checks.objects` が可変の静的データを禁じている（Research Log を参照）。
- **Alternatives Considered**:
  1. `{ const char *name; ... }` の配列（cmark の形）
  2. 名前を 1 つにつないだ文字の配列と、`{ 位置, 長さ, UTF-8 }` の配列
- **Selected Approach**: 2。
- **Rationale**: 再配置が要らないので、動的ライブラリでも表が読み取り専用の領域に置かれ、プロセスの間で共有される。項目が小さくなる（10 バイト）。
- **Trade-offs**: 位置を 16 ビット、長さを 8 ビットで持つので、名前の合計は 65,535 バイトまで、1 つの名前は 255 バイトまでになる。今の値（14,004 と 31）には十分な余裕がある。超えたときは、生成スクリプトが理由を示して失敗する。
- **Follow-up**: `checks.objects` が通ることを、実装のときに確かめる。

### Decision: case folding は、状態 C の表と状態 F の表に分ける
- **Context**: 要件 6。C は 1,501 項目で対応先が 1 つ、F は 105 項目で対応先が 2 つか 3 つである。
- **Alternatives Considered**:
  1. 1 つの表 `{ from, to[3] }`（1,606 × 16 バイト = 約 26 KB）
  2. C の表 `{ from, to }` と F の表 `{ from, count, to[3] }`（12,008 + 1,680 バイト = 約 14 KB）
  3. 連続する対応を範囲と差分にまとめる
- **Selected Approach**: 2。先に C の表を引き、なければ F の表を引き、なければ渡された値を返す。
- **Rationale**: 大きさが 1 のほぼ半分になり、生成も参照も単純なままである。3 はさらに小さくなるが、生成と参照が複雑になり、見返りは数 KB にとどまる。
- **Trade-offs**: 対応のないコードポイントは、2 つの表を引く（比較は合わせて 18 回まで）。ASCII は表を引かずに答えるので、多くの入力では影響しない。
- **Follow-up**: なし。

### Decision: 実体名の参照は、UTF-8 にしたバイト列を表に持ち、そこへのポインタを返す
- **Context**: 要件 7.1 と 7.8。
- **Alternatives Considered**:
  1. 表にコードポイントを持ち、参照のたびに UTF-8 にして、呼び出す側の領域に書く
  2. 表に UTF-8 のバイト列を持ち、表の中へのポインタと長さを返す
- **Selected Approach**: 2。
- **Rationale**: 参照のたびの変換がなく、呼び出す側は領域を用意しなくてよい。返すポインタは静的な読み取り専用のデータを指すので、解放が要らず、寿命の心配もない。UTF-8 への変換は生成スクリプトが行うので、ライブラリに符号化の関数を足さずに済む。
- **Trade-offs**: 項目が 6 バイトの領域を持つ（多くの実体は 2〜3 バイトしか使わない）。
- **Follow-up**: なし。

### Decision: 一致の検査は、テストが入力データを自分で読んで行う
- **Context**: 要件 8.10 は、全コードポイントと全実体名の一致を「入力データから作った期待値」で確かめることを求める。
- **Alternatives Considered**:
  1. 生成スクリプトが、期待値のファイルも生成する
  2. 単体テストが、`tools/data/` のファイルを実行時に読み、素朴な方法で期待値を作る
  3. 関数の結果を書き出す実行ファイルを作り、Python で入力データと比べる
- **Selected Approach**: 2。テストは、行を 1 つずつ読んで、コードポイントごとの配列（期待値）を埋める。その後、0 から U+10FFFF までのすべての値について関数の結果と比べる。
- **Rationale**: 期待値が生成スクリプトと独立に作られるので、生成スクリプトの誤りも見つかる。Python がない環境でもテストが動く（要件 1.8）。3 つのファイルの書式は行ごとに規則的で、C で読むのは難しくない。
- **Trade-offs**: テストに、入力データを読むコードが要る。entities.json は、JSON の全体ではなく、このファイルの実際の形（1 行に 1 項目）を前提に読む。形が変わったらテストが失敗し、それで気づける。
- **Follow-up**: 全コードポイントの検査の時間が、サニタイザ付きの構成でも数秒に収まることを確かめる。

### Decision: 最新であることの検査は、スクリプトの `--check` で行い、CI の専用のジョブと CTest の両方から呼ぶ
- **Context**: 要件 3.1〜3.4 と 1.2。
- **Alternatives Considered**:
  1. CI で再生成して `git diff --exit-code` を見る
  2. スクリプトに `--check` を持たせ、メモリ上で作った結果とファイルを比べる
- **Selected Approach**: 2。CI には専用のジョブを 1 つ足す（必ず実行される）。CTest には、Python 3.9 以上が見つかったときだけ `checks.generated_tables` を登録する。
- **Rationale**: `--check` はファイルを書き換えないので、手元の作業中の変更を壊さない（要件 3.4）。Git がなくても動く。CTest に登録すると、Windows と macOS のランナーでも一致が確かめられる（要件 1.2）。専用のジョブがあるので、Python が見つからずに CTest の登録が抜けても、検査が黙って消えることはない。
- **Trade-offs**: 検査の入口が 2 つになる。どちらも同じコマンドを呼ぶだけである。
- **Follow-up**: なし。

### Decision: 第三者の表示は `LICENSE` にまとめる
- **Context**: 要件 9.1、9.2、9.4。
- **Alternatives Considered**:
  1. `LICENSE` の MIT の本文の後ろに、第三者のデータの節を足す
  2. 別のファイル（`THIRD-PARTY-NOTICES` など）を作る
- **Selected Approach**: 1。
- **Rationale**: 要件と brief が「ライセンスのファイルに含める」と書いている。`LICENSE` はすでにインストールの対象なので、要件 9.4 のためのビルドの変更が要らない。cmark の `COPYING` も同じ形である。
- **Trade-offs**: GitHub のライセンスの自動判定が、MIT と認識しなくなるかもしれない。MIT の本文は先頭にそのまま残し、README にも MIT と書く。
- **Follow-up**: マージの後、リポジトリのページでの表示を確かめる。判定が外れて困る場合は、2 に切り替える（インストールの規則に 1 行足す）。

### Synthesis の結果
- **Generalization**: 句読点の判定と空白の判定は、どちらも「コードポイントが範囲の表に入っているか」である。範囲の表を引く関数を 1 つ作り、2 つの判定で使う。case folding の 2 つの表と実体名の表は、キーの型と比べ方が違うので、無理に 1 つの探索にまとめない。
- **Build vs. Adopt**: 表と関数は自分で作る（steering の決定）。生成の道具は Python の標準ライブラリを使い、gperf などは足さない。
- **Simplification**:
  - 版の記録のためのマニフェストを作らない。Unicode の版はファイルの 1 行目から、entities.json の同一性は SHA-256 から得る。
  - 実体名の最大の長さなどの定数を、内部ヘッダに出さない。長すぎる名前は、探索で自然に「見つからない」になる。
  - 空白の判定のうち、タブ・LF・FF・CR の 4 文字は CommonMark の規則なので、手書きの側に置く。生成するのは Zs の範囲だけである。
  - 参照する関数は、project-foundation の UTF-8 の部品を使わない。コードポイントを受け取り、UTF-8 は生成のときに作ってあるからである。

## Risks & Mitigations
- MSVC は、1 つの文字列リテラルの長さに上限がある（つなぐ前の 1 片が約 2,048 バイト、つないだ後が 65,535 バイト）。実体名の配列（14,004 バイト）は、短い文字列リテラルを並べた形で書き出す。生成スクリプトは、合計が 65,535 バイトを超えたら失敗する。
- 文字の配列を、終端の NUL を除いたちょうどの長さで初期化する書き方は、C では正しいが、一部のコンパイラが警告する。配列の長さを指定せず、終端の NUL を 1 バイト持たせる。
- Windows で行末が変換されると、一致の検査が失敗する。`.gitattributes` で防ぐ。
- 将来の UCD で、状態 F の対応先が 3 つを超える、または書式が変わる可能性がある。生成スクリプトが、ファイルと行を示して失敗する。黙って誤った表を作らない。
- GitHub のライセンスの自動判定が外れる可能性がある。上の Decision に書いた。
- entities.json は版を持たず、URL の内容が変わりうる。SHA-256 を生成物の先頭に書き、取得した日を `tools/data/README.md` に書く。実際には、この表は長く変わっていない。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — Unicode の句読点と空白の定義（2.1 節）、文字参照（2.5 節）、ラベルの照合（リンク参照定義）
- [DerivedGeneralCategory.txt 18.0.0](https://www.unicode.org/Public/18.0.0/ucd/extracted/DerivedGeneralCategory.txt) — General Category の範囲
- [CaseFolding.txt 18.0.0](https://www.unicode.org/Public/18.0.0/ucd/CaseFolding.txt) — case folding の対応と状態の説明
- [entities.json](https://html.spec.whatwg.org/entities.json) — HTML5 の実体名
- [Unicode License v3](https://www.unicode.org/license.txt) — UCD の利用の条件
- [HTML Standard の表示](https://html.spec.whatwg.org/multipage/acknowledgements.html) と [whatwg/html の LICENSE](https://github.com/whatwg/html/blob/main/LICENSE) — entities.json の利用の条件
- `tests/checks/CheckObjects.cmake`、`tests/checks/CheckLicenseHeaders.cmake`、`CONTRIBUTING.md` — 生成物が従う既存の検査と規約
