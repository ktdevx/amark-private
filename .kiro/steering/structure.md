# Project Structure

> 注：コードはまだない。ここに書いた配置と命名は、最初の spec（project-foundation）の設計で確定させる出発点である。確定したらこの注を消す。

## Organization Philosophy

責任ごとにモジュールを分け、依存は一方向にする。公開するものと内部のものを、ディレクトリではっきり分ける。モジュールの分け方は、spec の分け方（`.kiro/specs/`）と対応させる。

依存の向き（左が右に依存する）：

```
拡張点 → インラインの解析 → ブロックの解析 → 構文木 → 内部の部品
                  文字列の取り出し ↗        Unicode の表 ↗
```

- 下の層は上の層を知らない。構文木は解析器を知らず、内部の部品は Markdown を知らない。
- テスト専用のコード（HTML 出力など）は、ライブラリのどの層からも参照されない。

## Directory Patterns

### 公開ヘッダ
**Location**: `/include/amark/`
**Purpose**: 利用者が使う API だけを置く。不透明な型と関数の宣言、設定・エラー・診断の型。構造体のレイアウトは置かない。
**Example**: `#include <amark/amark.h>`

### ライブラリのソース
**Location**: `/src/`
**Purpose**: 実装と内部ヘッダ。内部ヘッダはインストールしない。1 つのモジュールは、`.c` と内部ヘッダの組にする。
**Example**: `src/arena.c` と `src/arena.h`

### 生成した表
**Location**: `/src/`（生成物）と `/tools/`（生成スクリプトと入力データ）
**Purpose**: Unicode の表と実体名の表。生成物はコミットし、手で編集しない。先頭に、生成元とスクリプトの名前を書く。
**Example**: `tools/` のスクリプトが UCD から `src/` の表を作る。

### テスト
**Location**: `/tests/`
**Purpose**: Naos のテスト。ライブラリのモジュールごとの単体テストと、全体の性質のテスト。テスト専用のコード（HTML 出力、spec.txt の取り込み）もここに置く。
**Example**: `tests/` の各テストを `naos_add_executable` で CTest に登録する。

### ビルドと CI
**Location**: `/CMakeLists.txt`、`/cmake/`、`/.github/workflows/`
**Purpose**: ビルドの設定、パッケージの設定ファイルのひな形、CI のワークフロー。

## Naming Conventions

- **公開する名前**：関数と型は `amark_` で始める。マクロと列挙の定数は `AMARK_` で始める。
- **内部の名前**：複数のファイルから使う内部の関数も、公開する名前と同じく `amark_` で始める（静的ライブラリでの名前の衝突を避けるため）。名前の形では公開と内部を区別しない。区別は、宣言する場所（内部ヘッダ）と `AMARK_API` の有無で行う。1 つのファイルだけで使う関数は `static` にする。
- **型**：`typedef` した型の名前は末尾に `_t` を付ける（`amark_arena_t`、`amark_error_t`）。構造体と列挙のタグには付けない（`typedef struct amark_arena { ... } amark_arena_t;`）。関数ポインタの型も同じである。
- **ファイル**：小文字とアンダースコア（`block_parser.c`）。
- **CMake**：プロジェクトとパッケージの名前は `Amark` である（`find_package(Amark)`、`AmarkConfig.cmake`）。利用者に渡すターゲットは `Amark::amark`。ライブラリのターゲットとファイルの名前は `amark` である。
- **エクスポート**：公開する関数には `AMARK_API` を付ける。動的ライブラリでは、それ以外のシンボルを隠す。

## Include Organization

```c
/* 利用者とテスト：公開ヘッダは山括弧で */
#include <amark/amark.h>

/* ライブラリの内部：内部ヘッダは引用符で */
#include "arena.h"
```

- 公開ヘッダは、内部ヘッダを取り込まない。
- 公開ヘッダは、それだけで取り込める（必要な標準ヘッダを自分で取り込む）。C++ からも使えるよう `extern "C"` で囲む。

## Code Organization Principles

- **資源の持ち主は文書**：確保したものはすべて、文書かそのアリーナに属する。個別に解放するものを作らない。
- **木を作るのは内部 API だけ**：解析器と方言は、構文木の内部 API を通してノードを作る。不変条件（範囲は内容の中、子は親の内側、兄弟は重ならず原文の順）は、この API と検査の関数で守る。
- **原文は複製しない**：ノードも属性も、原文を指す範囲で持つ。文字列が要るときは、文字列の取り出しの API で作る。
- **ソースの先頭**：各ファイルに、MIT の表示と「The Amark Authors」の著作権表示を付ける。

---
_Document patterns, not file trees. New files following patterns shouldn't require updates_
