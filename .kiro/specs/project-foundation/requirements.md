# Requirements Document

## Project Description (Input)
**誰が困っているか**: amark の開発者と、amark を自分のプロジェクトに組み込む利用者。そして、解析器を構成するほかのすべての spec（unicode-tables、syntax-tree など）。

**現在の状況**: リポジトリには `.kiro` と `.claude` しかなく、ビルド設定、CI、ライセンス、ソースはまだない。amark を Windows・Linux・macOS で、静的ライブラリと動的ライブラリの両方としてビルドし、テストし、配布する土台がない。また、解析器のどの部分も頼れる、メモリ管理と UTF-8 の扱いの共通部品がない。

**何を変えるか**:
- CMake（3.21 以上）で、静的と動的のライブラリ（`amark_static` と `amark_shared`、または `BUILD_SHARED_LIBS`）、テスト、インストールと `find_package(amark)` 用の設定ファイルを作れるようにする。エクスポートマクロ `AMARK_API` を生成する。
- GitHub Actions の CI で、Windows（MSVC と MinGW）、Linux（GCC と Clang）、macOS（Clang）のビルドとテストを回す。サニタイザ付きの構成も含める。
- Naos を `v0.4.0` に固定して FetchContent で取り込み、`naos_add_executable` で登録したテストを CTest で実行できるようにする。
- LICENSE（MIT、The Amark Authors）を置き、ソースの先頭に著作権表示を付ける規約と README の骨子を用意する。
- 公開しないヘッダに内部部品を置く。どれも利用者が指定した確保関数を使い、グローバルな状態を持たず、確保の失敗を戻り値で伝える。
  - 確保関数の抽象（既定は malloc と free）
  - アリーナ（文書ごとにまとめて解放する）
  - 可変長の配列
  - 行の索引（オフセットから行番号を求める）
  - UTF-8 の検査（不正な列を報告するモードと、U+FFFD に置き換えた結果を作るモード）
- 各部品に単体テストを付ける。

**制約**: C99、コンパイラ拡張を使わず、MSVC でもビルドできること。資源はすべて文書かアリーナに属する。Naos は 1.0 前なのでタグかコミットに固定する。

**範囲外**: Unicode の性質の表（unicode-tables）、公開する解析 API と木の型（syntax-tree）、解析の規則、I/O と BOM の除去、リリースの自動化。

## Requirements
<!-- Will be generated in /kiro-spec-requirements phase -->
