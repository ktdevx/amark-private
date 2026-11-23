# Research & Design Decisions

## Summary
- **Feature**: `project-foundation`
- **Discovery Scope**: New Feature（リポジトリにコードがない状態からの新規作成）
- **Key Findings**:
  - Naos `v0.4.0` は、テストの実行ファイルをビルドの直後に実行してテストの一覧を得る。実行ファイルが起動できないと、ビルドそのものが失敗する。動的ライブラリにリンクするテストは、DLL を実行ファイルと同じ場所に置く必要がある。
  - Naos は、確保の失敗の注入も、解放もれの追跡も持たない。テスト用の確保関数は、この spec が用意する。
  - この spec には公開する解析の関数がない。動的ライブラリに公開シンボルが 1 つもないと、Windows でインポートライブラリができない。このため、最小の公開関数（`amark_version_string`）を置く。

## Research Log

### Naos v0.4.0 の使い方と制約
- **Context**: テストの基盤（要件 3）と CI（要件 4）の設計に、Naos の CMake の関数とテストの書き方が要る。
- **Sources Consulted**: `https://github.com/vabelorg/naos` のタグ `v0.4.0` を取得し、`CMakeLists.txt`、`cmake/Naos.cmake`、`README.md`、`docs/`、`CHANGELOG` を読んだ。コードは実行していない。
- **Findings**:
  - タグ `v0.4.0` はコミット `09fde7172c4b63d416c32e98499cb0c0306d3dea` を指す。MIT ライセンス。
  - `cmake_minimum_required(VERSION 3.21)`。C99（拡張なし）。GCC、Clang、MSVC、MinGW-w64（MSYS2 の UCRT64 と MINGW64）を CI で確かめている。
  - ターゲットは `naos` と別名 `Naos::naos`。サブプロジェクトでは常に静的ライブラリになり、自分のテストとインストールの規則は既定で無効になる。全体に効くコンパイルの指定は加えない。
  - `naos_add_executable(<target> SOURCES ... [LIBRARIES ...] [PREFIX ...] [LABELS ...] [PROPERTIES ...] [DISCOVERY_TIMEOUT ...] [ALLOW_NO_ASSERTIONS])`。`FetchContent_MakeAvailable` の直後から使える。`enable_testing()` は呼ばないので、利用する側が呼ぶ。
  - テストの検出はビルド時に行う。`POST_BUILD` で `<exe> --naos-list` を実行する。CTest では、テスト 1 つが 1 つのプロセスになる。ビルドの前は `<target>.NOT_BUILT` という失敗するテストが 1 つある。
  - テストは `NAOS_TEST`、`NAOS_SUITE`、`NAOS_MAIN` で書く。登録は明示的で、`main` は自分のソースに置く。setup と teardown はテストごとにある。
  - `NAOS_REQUIRE_*`、`NAOS_FAIL`、`NAOS_SKIP` は `longjmp` でテストを抜ける。後始末は teardown に置く。
  - 0.4.0 から、検査を 1 つも実行しないテストは失敗する。
  - 確保の失敗の注入、解放もれの追跡、モックはない。
  - Unix で静的な Naos を使うとき `-lm` が要る場合があるが、CMake のターゲットは `m` をリンクしていない。実際にリンクが失敗するかは確かめていない。
  - GitHub の issue は読めなかった（この環境からのアクセスが許可されていない）。既知の不具合は CHANGELOG の範囲でしか分からない。
- **Implications**:
  - テストの実行ファイルは、内部の関数を試すものを静的ライブラリにリンクする。DLL に依存しないので、検出が安定する。
  - 公開 API を試すテストは `amark` にリンクする。実行ファイルと DLL の出力先を 1 つのディレクトリにそろえる。
  - テスト用の確保関数（数える、指定した回で失敗させる）を `tests/support/` に置く。
  - `GIT_TAG` にはコミットのハッシュを書く。

### 動的ライブラリの公開範囲と、内部の関数のテスト
- **Context**: 動的ライブラリでは `AMARK_API` を付けたシンボルだけを公開する（要件 1.4）。一方、単体テストは内部の関数を呼ぶ（要件 3.8）。
- **Sources Consulted**: CMake の文書（`GenerateExportHeader`、`C_VISIBILITY_PRESET`、`OBJECT` ライブラリ、`TARGET_RUNTIME_DLLS`）。
- **Findings**:
  - `generate_export_header` は、コンパイラごとの属性と、静的ライブラリ用の定義（`<BASE>_STATIC_DEFINE`）を扱う。
  - OBJECT ライブラリを共有する方法では、オブジェクトを位置独立にし、エクスポート用の定義を手で付ける必要がある。
  - 公開シンボルのない DLL は、MSVC でインポートライブラリを作らない。
- **Implications**: テスト専用の静的ライブラリを同じソースの一覧から作る。最小の公開関数を 1 つ置く。

### UTF-8 の正しさの定義と置換の単位
- **Context**: 要件 10.3 と 11.2。
- **Sources Consulted**: Unicode 標準 3.9 節（表 3-7 と「U+FFFD Substitution of Maximal Subparts」）、WHATWG Encoding Standard の UTF-8 デコーダ。
- **Findings**:
  - 表 3-7 が、正しい列の 2 バイト目の範囲を先頭バイトごとに定める（`E0` は `A0..BF`、`ED` は `80..9F`、`F0` は `90..BF`、`F4` は `80..8F`）。
  - 置換の単位は「正しい列の先頭として続けられる最長の部分、なければ 1 バイト」である。WHATWG のデコーダと同じ結果になる。
- **Implications**: 検査と置換が 1 つの走査の関数を共有する。テストは、標準の例で確かめる。

### C99 での整列
- **Context**: アリーナは、どの型にも使える境界でメモリを返す（要件 7.1）。C99 には `max_align_t` と `_Alignof` がない。
- **Findings**: 最も厳しい整列を持つ型（`long long`、`long double`、`void *`、関数ポインタ）の共用体を構造体に入れ、`offsetof` で整列を求める方法が、C99 の範囲で使える。
- **Implications**: `AMARK_ARENA_ALIGN` をこの方法で定める。SIMD 用のより大きな整列は対象にしない。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 単一のターゲットと `BUILD_SHARED_LIBS` | `amark` 1 つ。種類は構成で選ぶ | 利用者の書き方が 1 つ。CMake の標準 | 1 回のビルドでは片方だけ | 採用。CI が両方をビルドする |
| `amark_static` と `amark_shared` | 1 回のビルドで両方を作る | 1 回で両方が得られる | 利用者がターゲットを選ぶ必要がある。エクスポートの設定が 2 組になる | brief が挙げた候補。採らない |
| テスト専用の静的ライブラリ | 同じソースを 2 回コンパイルする | 公開範囲に影響しない。テストが DLL に依存しない | コンパイルの時間が増える | 採用 |
| OBJECT ライブラリの共有 | 1 回のコンパイルを 2 つで使う | コンパイルが 1 回 | 位置独立とエクスポートの定義を手で扱う | 採らない |
| 配列の記憶域をアリーナに限る | 伸びるたびにアリーナから取り直す | 個別の解放がない。失敗しても状態が変わらない | 古い場所がアリーナに残る（合計は最後の容量より小さい） | 採用 |
| 配列の記憶域を確保関数から直接取る | 伸びるたびに確保・複写・解放 | 無駄が残らない | 個別の解放が要り、「資源は文書かアリーナに属する」に合わない | 採らない |

## Design Decisions

### Decision: ライブラリのターゲットを 1 つにする
- **Context**: 要件 1.2、2.5、2.6。
- **Alternatives Considered**:
  1. `amark` 1 つ（`BUILD_SHARED_LIBS`）
  2. `amark_static` と `amark_shared`
- **Selected Approach**: 1。別名 `Amark::amark` を用意する。
- **Rationale**: 利用者が、インストールでも直接の取り込みでも、静的でも動的でも、同じ名前を使える。
- **Trade-offs**: 両方を配布するには 2 回ビルドする。
- **Follow-up**: 1 つのインストール先に両方を置く必要が出たら、設定ファイルを分ける方法を別に検討する。

### Decision: 最小の公開関数を置く
- **Context**: Windows の動的ライブラリと、利用側のテスト（要件 4.6）。
- **Alternatives Considered**:
  1. `amark_version_string` を置く
  2. この spec では公開関数を置かず、動的ライブラリの確認を syntax-tree まで待つ
- **Selected Approach**: 1。
- **Rationale**: 動的ライブラリのビルド、インストール、利用側からのリンクを、この spec の CI で確かめられる。版の情報は、どのライブラリにもある最小の API である。
- **Trade-offs**: brief にない公開関数が 1 つ増える。
- **Follow-up**: syntax-tree が公開 API を足すとき、`amark.h` の構成を確かめ直す。

### Decision: 確保関数の形
- **Context**: 要件 6。brief は「既定は malloc と free」とする。
- **Alternatives Considered**:
  1. `malloc_fn(ctx, size)` と `free_fn(ctx, ptr)`
  2. `realloc` を加える
  3. `free_fn` に大きさを渡す
- **Selected Approach**: 1。
- **Rationale**: 配列がアリーナの上にあるので、`realloc` の使い道がない。利用者が実装する関数が最も少ない。
- **Trade-offs**: 大きさを要る確保器（大きさを覚えないプール）は、利用者が自分で大きさを記録する。
- **Follow-up**: syntax-tree が公開するとき、型を公開ヘッダへ移す。

### Decision: 失敗したときに何も確保していない状態にする
- **Context**: 要件 9.10、11.8、12.10。
- **Selected Approach**: 行の索引と置換は、先に必要な大きさを数え、アリーナからの確保を 1 回にする。配列は、新しい場所を得てから状態を書き換える。
- **Rationale**: 巻き戻しの処理が要らない。失敗の注入のテストが単純になる。
- **Trade-offs**: バイト列を 2 回走査する。時間は長さに比例したままである。

### Decision: `assert` を使わない
- **Context**: steering は `abort` を禁じる。`assert` は `abort` を呼ぶ。
- **Selected Approach**: 前提条件は呼び出す側の責任とし、内部ヘッダの注釈に書く。違反はテスト、サニタイザ、レビューで見つける。`checks.objects` が、`abort` への参照がないことを確かめる。
- **Trade-offs**: 開発中に違反が早く見つからないことがある。

### Decision: 製品の性質を検査で守る
- **Context**: 要件 12.1、12.4、1.4。後続の spec でも守り続ける必要がある。
- **Selected Approach**: `nm` の出力を調べる CMake のスクリプトを CTest に登録する（Linux だけ）。
- **Rationale**: レビューだけに頼らず、変更のたびに確かめられる。道具は Linux の標準の `nm` だけで足りる。
- **Trade-offs**: Windows と macOS では走らない。コードは共通なので、Linux の検査で足りる。

### Synthesis の結果
- **Generalization**: 行の索引、置換の結果、配列は、どれも「アリーナから取り、個別に解放しない」という同じ形にそろえた。失敗時の扱いも同じになった。
- **Build vs. Adopt**: エクスポートマクロ、パッケージの設定、インストール先は、CMake の付属のモジュールを使う。UTF-8 と Unicode については、steering が外部の部品（utf8proc、ICU）を採らないと決めている。アリーナと配列は小さく、確保関数の差し替えが要るので自作する。
- **Simplification**: CMakePresets、`realloc`、配列の 2 種類の記憶域、行の開始位置を返す関数、UTF-8 の符号化の関数は、要件にないので置かない。

## Risks & Mitigations
- Naos は 1.0 前で、マイナー版で互換が壊れうる — コミットのハッシュで固定する。版を上げるときは CHANGELOG を読み、全構成の CI を通す。
- Naos の issue を読めていない — 実装の最初に、空のテスト 1 つを 5 つの構成で通す。問題が出たら、その時点で対処する。
- Unix で `libm` のリンクが要る可能性 — リンクが失敗したら、テストのターゲットに `m` を足す。
- MSVC の C99 対応 — 可変長の配列や複素数など、MSVC が対応しない機能を使わない。MSVC のジョブが毎回確かめる。
- `AMARK_ARENA_ALIGN` が、より大きな整列を要る型に足りない — 対象は C の標準の型とする。内部ヘッダに明記する。
- MinGW のジョブの環境（MSYS2）が変わる — `msys2/setup-msys2` で必要なパッケージを明示して入れる。

## References
- [Naos](https://github.com/vabelorg/naos) — テストフレームワーク。タグ `v0.4.0`
- [CMake: GenerateExportHeader](https://cmake.org/cmake/help/latest/module/GenerateExportHeader.html) — エクスポートマクロの生成
- [CMake: CMakePackageConfigHelpers](https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html) — パッケージの設定と版の互換
- [CMake: FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) — 依存の取り込み
- [The Unicode Standard, Chapter 3 Conformance](https://www.unicode.org/versions/latest/core-spec/chapter-3/) — 表 3-7 と U+FFFD の置換
- [WHATWG Encoding Standard](https://encoding.spec.whatwg.org/#utf-8-decoder) — UTF-8 デコーダ
- [CommonMark 0.31.2](https://spec.commonmark.org/0.31.2/#line-ending) — 行末の定義
