# Brief: project-foundation

## Problem
amark を Windows・Linux・macOS で、静的ライブラリと動的ライブラリの両方としてビルドし、テストし、配布できる土台がない。また、解析器のどの部分も、メモリ管理と UTF-8 の扱いを共通の部品に頼る必要がある。この部品は確保関数を差し替えられ、グローバルな状態を持たないものでなければならない。

## Current State
リポジトリは空である（`.kiro` と `.claude` だけ）。ビルド設定、CI、ライセンス、ソースはまだない。

## Desired Outcome
- CMake で、静的と動的のライブラリ、テスト、インストールと `find_package(amark)` 用の設定ファイルを作れる。
- GitHub Actions の CI が、Windows（MSVC と MinGW）、Linux（GCC と Clang）、macOS（Clang）でビルドとテストを回す。サニタイザ付きの構成も含む。
- Naos を `v0.4.0` に固定して FetchContent で取り込み、CTest でテストを実行できる。
- LICENSE（MIT、The Amark Authors）がある。ソースの先頭に著作権表示を付ける規約がある。
- 内部の部品がある。どれも利用者が指定した確保関数を使い、グローバルな状態を持たない。
  - 確保関数の抽象（既定は malloc と free）
  - アリーナ。解放は文書ごとにまとめて行う。
  - 可変長の配列
  - 行の索引。オフセットから行番号を求める。
  - UTF-8 の検査。不正な列を報告するモードと、U+FFFD に置き換えた結果を作るモードがある。

## Approach
CMake のターゲットを `amark_static` と `amark_shared`（または `BUILD_SHARED_LIBS`）として組み、エクスポートマクロ（`AMARK_API`）を生成する。内部の部品は公開しないヘッダに置く。確保の失敗はすべて戻り値で伝える。テストは Naos の `naos_add_executable` で登録する。

## Scope
- **In**:
  - CMakeLists の構成。プロジェクトの最小バージョン、オプション、インストール、パッケージの設定ファイル。
  - CI のワークフロー
  - LICENSE、ソースの著作権ヘッダの規約、README の骨子
  - Naos の統合
  - 内部の部品（確保関数、アリーナ、配列、行の索引、UTF-8 の検査と置換）と、それらの単体テスト
- **Out**:
  - Unicode の性質の表（unicode-tables）
  - 公開する解析 API と木の型（syntax-tree）
  - リリースの自動化（パッケージの公開など）は今は扱わない。必要になったら追加する。

## Boundary Candidates
- ビルド・CI・ライセンス（リポジトリの基盤）
- 内部の部品のライブラリ（メモリと UTF-8）

## Out of Boundary
- 解析の規則、ノードの定義、Unicode の表
- I/O と BOM の除去（利用者側）

## Upstream / Downstream
- **Upstream**: Naos v0.4.0、CMake 3.21 以上、GitHub Actions
- **Downstream**: unicode-tables、syntax-tree、そのほかすべての spec

## Existing Spec Touchpoints
- **Extends**: なし
- **Adjacent**: unicode-tables（ビルドに生成された表を組み込む）、syntax-tree（確保関数の設定を公開 API に出す）

## Constraints
- C99。コンパイラの拡張を使わない。MSVC でもビルドできること。
- グローバルな状態を持たない。資源はすべて文書かアリーナに属する。
- Naos は 1.0 前なので、タグかコミットに固定する。
