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

## Introduction
unicode-tables は、CommonMark 0.31.2 の解析に要る文字のデータを、Amark の中に持たせる。データは 3 種類ある。Unicode の句読点と空白の判定、リンクのラベルを比べるための full case folding、HTML5 の実体名である。

この spec は 2 つの部分からなる。

1. **生成の仕組み**：UCD と entities.json から、表の C のソースを作るスクリプト。入力データと生成物はリポジトリにコミットする。通常のビルドでは生成を行わない。
2. **参照する関数**：表を引く内部の関数。コードポイントが句読点か空白かを判定し、コードポイントを畳み込み、実体名から文字を得る。

この文書で「開発者」は Amark 自体を開発する人、「呼び出す側」は参照する関数を使う後続の spec（block-parser、inline-parser、source-text）のコードを指す。「コードポイント」は、0 から U+10FFFF までの整数を指す。

## Boundary Context
- **In scope**:
  - 生成スクリプト、入力データ（DerivedGeneralCategory.txt、CaseFolding.txt、entities.json）、生成した表
  - 生成した表が最新であることを確かめる検査（手元と CI）
  - 参照する関数（句読点の判定、空白の判定、case folding、実体名の参照）と、その単体テスト
  - Unicode License v3 の表示と、入力データの出所と版の記録
- **Out of scope**:
  - UTF-8 の検査とデコード（project-foundation）。参照する関数は、デコード済みのコードポイントを受け取る。
  - リンクのラベルの正規化そのもの（block-parser）。前後の空白の除去、連続する空白の畳み込み、文字列全体の畳み込みと比較を含む。この spec は、1 つのコードポイントの畳み込みだけを行う。
  - 文字参照の構文の認識（inline-parser、source-text）。`&` と `;` の検出、数値文字参照（`&#35;`、`&#x22;`）の解釈、参照として認められなかったときの扱いを含む。この spec は、実体名を引くことだけを行う。
  - 強調の flanking の規則など、判定の結果の使い方（inline-parser）
  - 正規化（NFC など）、書記素の分割、文字幅、simple case folding、トルコ語の特別な畳み込み
- **Adjacent expectations**:
  - 参照する関数は公開しない。公開シンボルの一覧（project-foundation の検査）は変わらない。
  - 生成した表と参照する関数は、project-foundation のビルドの構成、テストの基盤、リポジトリの検査（著作権表示、可変の静的データがないこと、禁止した関数を使わないこと）に従う。
  - 呼び出す側は、不正な UTF-8 を先に処理し、コードポイントを渡す。ただし、参照する関数は、コードポイントの範囲の外の値を渡されても安全に答える（Requirement 8）。
  - 対象の Unicode の版は 18.0.0 である。版を上げる判断は、この spec の外で行う。この spec は、版を上げる作業が再生成だけで済むようにする。

## Requirements

### Requirement 1: 表の生成
**Objective:** As a 開発者, I want 入力データから表の C のソースを 1 つの手順で作りたい, so that 表を手で書かずに済み、だれが実行しても同じ結果になる

#### Acceptance Criteria
1. When 開発者が生成の手順を実行する, the 生成スクリプト shall リポジトリにある入力データ（DerivedGeneralCategory.txt、CaseFolding.txt、entities.json）だけから、句読点、空白、case folding、実体名の表の C のソースを書き出す
2. The 生成スクリプト shall 同じ入力データからは、実行する OS、実行する回数、実行する日時によらず、バイト単位で同じ生成物を作る
3. The 生成スクリプト shall ネットワークへの接続を必要としない
4. The 生成スクリプト shall 各生成物の先頭に、生成物であって手で編集しないこと、生成元の入力データの名前と版、スクリプトの名前を書く
5. The 生成スクリプト shall 各生成物の先頭に、リポジトリの規約に従った著作権表示を付ける
6. The 生成スクリプト shall CommonMark が使う性質（General Category の P と S と Zs、CaseFolding.txt の状態 C と F、`;` で終わる実体名）以外のデータを、生成物に含めない
7. If 入力データがない、または形式が想定と違う, then the 生成スクリプト shall 原因のファイル（分かる場合は行も）を示して 0 以外の終了コードで終わり、既存の生成物を書き換えない
8. The ビルド構成 shall コミットされた生成物をそのままコンパイルし、ライブラリとテストのビルドにスクリプトの実行環境を必要としない

### Requirement 2: 入力データと版の管理
**Objective:** As a 開発者, I want 表がどのデータのどの版から作られたかをリポジトリで確かめたい, so that Unicode の版を上げる作業が、データの差し替えと再生成だけで済む

#### Acceptance Criteria
1. The リポジトリ shall 生成スクリプト、入力データ、生成物をすべて含む
2. The リポジトリ shall DerivedGeneralCategory.txt と CaseFolding.txt を、同じ Unicode の版（この spec では 18.0.0）のものにする
3. The リポジトリ shall 入力データごとに、取得元と版（entities.json は取得元と取得した時点）を記録する
4. When 開発者が入力データを同じ形式の新しい版に差し替えて生成の手順を実行する, the 生成スクリプト shall スクリプトと参照する関数のソースを変えなくても、新しい版の表を作る
5. If DerivedGeneralCategory.txt と CaseFolding.txt の Unicode の版が一致しない, then the 生成スクリプト shall 2 つの版を示して 0 以外の終了コードで終わる
6. The リポジトリ shall 表を再生成する手順（必要な実行環境、実行するコマンド、入力データを差し替える方法）を、貢献者が参照できる文書に書く

### Requirement 3: 生成物が最新であることの検査
**Objective:** As a 開発者, I want コミットされた表が入力データとスクリプトに合っていることを自動で確かめたい, so that 手で編集した表や、再生成を忘れた表がマージされない

#### Acceptance Criteria
1. When 既定のブランチへのプッシュ、またはプルリクエストの作成か更新が起きる, the CI shall リポジトリの入力データとスクリプトから表を生成し直し、コミットされた生成物とバイト単位で比べる
2. If 生成し直した結果がコミットされた生成物と違う, then the CI shall その実行を失敗として報告し、違いのあるファイルを記録に残す
3. The リポジトリ shall CI と同じ検査を、開発者が手元で 1 つの手順で実行できるようにする
4. The 検査 shall リポジトリの中のファイルを書き換えない

### Requirement 4: 句読点の判定
**Objective:** As a 呼び出す側の開発者, I want コードポイントが CommonMark の「Unicode の句読点」かどうかを知りたい, so that 強調の flanking を仕様のとおりに判定できる

#### Acceptance Criteria
1. When General Category が P（Pc、Pd、Ps、Pe、Pi、Pf、Po）または S（Sm、Sc、Sk、So）のコードポイントが渡される, the 句読点の判定 shall 真を返す
2. When General Category が P でも S でもないコードポイントが渡される, the 句読点の判定 shall 偽を返す
3. The 句読点の判定 shall ASCII の句読点 32 文字（U+0021–U+002F、U+003A–U+0040、U+005B–U+0060、U+007B–U+007E）のすべてに真を返す
4. The 句読点の判定 shall 未割り当てのコードポイントとサロゲート（U+D800–U+DFFF）に偽を返す
5. The 句読点の判定 shall すべてのコードポイントについて、入力データの DerivedGeneralCategory.txt と一致する結果を返す

### Requirement 5: 空白の判定
**Objective:** As a 呼び出す側の開発者, I want コードポイントが CommonMark の「Unicode の空白」かどうかを知りたい, so that 強調の flanking を仕様のとおりに判定できる

#### Acceptance Criteria
1. When General Category が Zs のコードポイントが渡される, the 空白の判定 shall 真を返す
2. When U+0009（タブ）、U+000A（LF）、U+000C（FF）、U+000D（CR）のいずれかが渡される, the 空白の判定 shall 真を返す
3. When それ以外のコードポイントが渡される, the 空白の判定 shall 偽を返す
4. The 空白の判定 shall U+000B（垂直タブ）、U+0085、U+2028、U+2029 に偽を返す
5. The 空白の判定 shall どのコードポイントについても、句読点の判定と同時に真を返さない

### Requirement 6: case folding
**Objective:** As a 呼び出す側の開発者, I want 1 つのコードポイントを Unicode の full case folding で畳み込みたい, so that リンクのラベルを仕様のとおりに、大文字と小文字を区別せずに比べられる

#### Acceptance Criteria
1. When CaseFolding.txt に状態 C の対応があるコードポイントが渡される, the case folding shall その対応先の 1 つのコードポイントを結果にする
2. When CaseFolding.txt に状態 F の対応があるコードポイントが渡される, the case folding shall その対応先の 2 つまたは 3 つのコードポイントを、CaseFolding.txt の順で結果にする
3. When 状態 C の対応も状態 F の対応もないコードポイントが渡される, the case folding shall 渡されたコードポイントそのもの 1 つを結果にする
4. The case folding shall 状態 S と状態 T の対応を使わない
5. The case folding shall 結果を、呼び出す側が渡した領域に書き出し、書き出したコードポイントの数を呼び出す側に伝える
6. The case folding shall 1 回の呼び出しで、3 つを超えるコードポイントを書き出さない
7. The case folding shall U+1E9E（ẞ）と U+00DF（ß）をどちらも U+0073 U+0073（ss）に、U+0041（A）を U+0061（a）に、U+0390 を U+03B9 U+0308 U+0301 に畳み込む
8. The case folding shall すべてのコードポイントについて、入力データの CaseFolding.txt の状態 C と F から得られる結果と一致する結果を返す

### Requirement 7: 実体名の参照
**Objective:** As a 呼び出す側の開発者, I want HTML5 の実体名から、それが表す文字を得たい, so that 文字参照を仕様のとおりに解決できる

#### Acceptance Criteria
1. When entities.json に `;` で終わる形で載っている実体名が、先頭の `&` と末尾の `;` を除いた形で渡される, the 実体名の参照 shall 見つかったことと、その実体が表す 1 つまたは 2 つのコードポイントを UTF-8 で符号化したバイト列を返す
2. If 渡された名前が、entities.json に `;` で終わる形で載っていない, then the 実体名の参照 shall 見つからなかったことを返す
3. The 実体名の参照 shall 大文字と小文字を区別する（`Auml` と `auml` は別の実体で、`Amp` は見つからない）
4. The 実体名の参照 shall 名前を、開始位置と長さで指定したバイト列として受け取り、終端の NUL を必要としない
5. If 長さが 0 の名前、または最も長い実体名より長い名前が渡される, then the 実体名の参照 shall 見つからなかったことを返す
6. The 実体名の参照 shall `amp` に U+0026 を、`nbsp` に U+00A0 を、`NotEqualTilde` に U+2242 U+0338 の 2 つのコードポイントを返す
7. The 実体名の参照 shall entities.json に `;` で終わる形で載っているすべての実体名について、entities.json の `codepoints` と一致する結果を返す
8. The 実体名の参照 shall 返すバイト列を、呼び出す側が解放する必要のない形で渡す

### Requirement 8: 参照する関数に共通の性質
**Objective:** As a 呼び出す側の開発者, I want 参照する関数を、どこからでも準備なしに安全に呼びたい, so that 解析器が、状態を持たない、確保の失敗が起きないという Amark の性質を保てる

#### Acceptance Criteria
1. The 参照する関数 shall 同じ引数には常に同じ結果を返し、引数と結果のほかに何も読み書きしない
2. The 参照する関数 shall メモリを確保せず、失敗しない
3. The 参照する関数 shall 事前の初期化の呼び出しを必要としない
4. The 生成した表 shall 読み取り専用のデータであり、実行中に変更されない
5. The 参照する関数 shall 複数のスレッドから同時に呼ばれても、同期なしで正しい結果を返す
6. If コードポイントの範囲の外の値（U+10FFFF より大きい値）が渡される, then the 句読点の判定と空白の判定 shall 偽を返し、the case folding shall 渡された値そのもの 1 つを結果にする
7. The 参照する関数 shall 1 回の呼び出しの時間を、表の項目の数に比例させない（表の全体を順に調べない）
8. The 参照する関数 shall 再帰を使わない
9. The ビルド構成 shall 参照する関数を、動的ライブラリの公開シンボルに加えない
10. The テストの基盤 shall 参照する関数の単体テストを CTest に登録し、Requirement 4 から 7 の一致の条件（全コードポイント、全実体名）を、入力データから作った期待値で確かめる

### Requirement 9: 第三者のデータの表示
**Objective:** As a 利用者, I want Amark に含まれる第三者のデータの出所と利用の条件を確かめたい, so that Amark を配布するときに必要な表示が分かる

#### Acceptance Criteria
1. The リポジトリ shall ライセンスのファイルに、Unicode License v3 の全文と Unicode の著作権表示を、どのファイルに適用されるか（UCD の入力データと、そこから生成した表）とともに書く
2. The リポジトリ shall ライセンスのファイルに、entities.json の出所（WHATWG の HTML Standard）と、その利用の条件の表示を書く
3. The 生成スクリプト shall Unicode のデータから作った生成物の先頭に、データの出所が Unicode であることと、適用されるライセンスの名前を書く
4. When 開発者がインストールを実行する, the ビルド構成 shall 第三者のデータの表示を含むライセンスのファイルを、インストール先に置く
