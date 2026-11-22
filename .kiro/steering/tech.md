# Technology Stack

## Architecture

解析は 2 段階で行う。まずブロックを解析し、次にインラインを解析する（CommonMark の参照実装と同じ構成）。

- **木**：ノードはアリーナ上の平坦な配列に置き、親・最初の子・次の兄弟を添字でつなぐ。ノードは原文の範囲と行番号だけを持つ。
- **メモリ**：文書ごとにアリーナを 1 つ持ち、解放はまとめて行う。確保はすべて、設定で渡された確保関数を通す。
- **制御**：再帰を使わない。入れ子は明示的なスタックで扱う。
- **方言**：決まった差し込み口（ブロックの開始と継続、インラインの起動文字、後処理）に規則を登録する。規則が返した結果は、本体が不変条件を検査してから採用する。
- **公開 API**：不透明なハンドルと関数で構成する。構造体のレイアウトは公開しない（ABI を安定させるため）。

## Core Technologies

- **Language**: C99。コンパイラの拡張には頼らない。
- **Build**: CMake。テストのビルドには 3.21 以上が要る（Naos の要件）。
- **Artifacts**: 静的ライブラリと動的ライブラリの両方。
- **Platforms**: Windows（MSVC、MinGW）、Linux（GCC、Clang）、macOS（Clang）。
- **CI/CD**: GitHub Actions。3 つの OS でビルドとテストを回す。
- **License**: MIT。著作権表示は「The Amark Authors」。

## Key Libraries

- **Naos**（https://github.com/vabelorg/naos）：テストフレームワーク。1.0 前なので、タグ `v0.4.0` かコミットに固定して FetchContent で取り込む。テストは CTest に登録する。
- **実行時の外部依存はない**。Unicode の表と HTML5 の実体名の表は、UCD と entities.json から生成スクリプトで作り、生成物をコミットする（ビルド時には生成しない）。

## Development Standards

### 準拠
- 対象は CommonMark 0.31.2。spec.txt の全例を通す。
- Unicode の性質は仕様に合わせる。句読点は General Category の P と S、空白は Zs とタブ・LF・FF・CR、case folding は CaseFolding.txt の C と F。

### エラーの扱い
- 失敗は戻り値で伝える。`abort`、`exit`、`longjmp` は使わない。
- 確保の失敗は、すべての呼び出しで検査する。失敗したら確保したものをすべて解放し、部分的な結果を返さない。
- 方言の規則の不正な結果は、エラーではなく診断にする。解析は続ける。

### 状態と並行性
- グローバルな変数、静的な可変の状態、I/O を持たない。表は const にする。
- 入力のバイト列を変更しない。

### 性能
- 時間は入力の長さにほぼ比例する。病的な入力（深い入れ子、大量の区切り、閉じない括弧など）でも守る。
- 再帰を使わない。木の走査と解放も同じである。

### Code Quality
- 警告を多めに有効にし、CI では警告をエラーにする（GCC と Clang は `-Wall -Wextra -Wpedantic`、MSVC は `/W4`）。
- CI にサニタイザ付きの構成（ASan、UBSan）を含める。

### Testing
- 各モジュールの単体テストは、そのモジュールの spec で書く。
- 全体の性質は、次のテストで確かめる。
  - spec.txt の準拠
  - 病的な入力
  - 原文の復元
  - 木の不変条件
  - 確保の失敗の注入
  - 決定性と並行性
- spec.txt と比べるための HTML 出力はテスト専用で、ライブラリには含めない。

## Development Environment

### Required Tools
- C99 のコンパイラ（GCC、Clang、MSVC のいずれか）
- CMake 3.21 以上
- 表を再生成するときだけ、スクリプトの実行環境（通常のビルドには要らない）

### Common Commands
```bash
# Configure: cmake -S . -B build
# Build:     cmake --build build
# Test:      ctest --test-dir build --output-on-failure
```

## Key Technical Decisions

- **2 段階の解析と平坦な木を選んだ**：原文を複製しない、範囲の不変条件、再帰を使わない、失敗したら全部解放する、の各性質を素直に満たせる。イベント駆動（md4c 型）は、参照の解決と方言の結果の検査が複雑になるので採らなかった。既存実装のフォークも、原文を複製する設計が要件と合わないので採らなかった。
- **Unicode の表は自前で生成する**：外部依存がなく、確保関数とグローバル状態の問題も起きない。utf8proc は確保関数を差し替えられず、大きい（約 350 KB）ので採らなかった。
- **HTML 出力はテスト専用にする**：出力は利用者の責任とし、本体を解析に集中させる。
- **汎用の部品は内部に置く**：アリーナ、可変長の配列、行の索引、UTF-8 の検査は公開しない。
- **規則は設定で変えられない**：設定は、解析の対象と資源の扱いだけを変える。構文の解釈は常に同じである。
- **第三者のデータの表示**：Unicode の表のデータには、Unicode License v3 の表示を付ける。

---
_Document standards and patterns, not every dependency_
