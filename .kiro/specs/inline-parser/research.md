# Gap Analysis: inline-parser

要件（`requirements.md`、未承認）と、block-parser までの実装（`src/`、`tests/`）の差を調べた。決定はせず、設計で選ぶための材料と選択肢を並べる。

## 1. 現状

### 使える部品

| 部品 | 場所 | インラインの解析での使い道 |
|---|---|---|
| 解析の入口 | `src/parse.c` の `parse_document` | `amark_block_parse` の後、作業用アリーナ `scratch` を解放する前に、インラインの解析の呼び出しを 1 行足せる。失敗したときの文書の破棄は、すでにここにある。 |
| 内容の範囲の一覧 | `src/parse_data.h` の `amark_inline_content_count`、`amark_inline_content_at` | 段落と見出しのノードと、その内容の範囲 `[start, end)` を、文書の順に得る。 |
| 行の記録 | `src/line.h` の `amark_line_bounds`、`amark_line_get` | 行の内容の終わりと、次の行の `content_start` を、行の数によらない時間で得る。 |
| ラベルと定義の表 | `src/definition.h` の `amark_label_scan`、`amark_definitions_find` | 参照のラベルを読み、定義を引く。どちらも確保せず、失敗しない。行をまたぐ処理と、999 文字の上限、U+0000 と不正なバイト列の扱いを含む。 |
| 木を作る API | `src/tree_internal.h` | `amark_tree_append`、`amark_tree_insert_before`、`amark_tree_wrap`、`amark_tree_remove`、`amark_tree_set_range`、`amark_tree_set_link`、`amark_tree_set_autolink`。インラインの 12 種類の ID は `include/amark/tree.h` にある。 |
| 属性の読み出し | `include/amark/tree.h` の `amark_link_*`、`amark_autolink_type` | 公開 API はそろっている。足す関数はない。 |
| Unicode の判定 | `src/unicode.h` | `amark_unicode_is_punctuation`、`amark_unicode_is_whitespace`。 |
| 実体名 | `src/entity.h` の `amark_entity_lookup` | 名前が長すぎても安全に「ない」を返す。 |
| UTF-8 | `src/utf8.h` の `amark_utf8_decode_lossy` | 前へ進む向きのデコード。不正なバイト列を U+FFFD として読む。 |
| 作業用のメモリ | `src/arena.h`、`src/array.h` | 区切りのスタックと括弧のスタックを `scratch` に置ける。 |
| 不変条件の検査 | `src/tree_check.h` | インラインのノードの範囲、入れ子、リンクの属性を、すでに検査する。 |
| テストの支援 | `tests/support/test_alloc.h`、`tree_dump.h` | 確保の失敗の注入。木を 1 行の文字列にする関数（今はブロックだけ）。 |

### 守る決まり

- モジュールは `src/<name>.c` と `src/<name>.h` の組。`src/CMakeLists.txt` の `AMARK_SOURCES` に 1 行足す。
- 複数のファイルから使う関数は `amark_` で始める。公開ヘッダと `tests/checks/exported_symbols.txt`（今は 46 行）は変えない（要件 18.7）。
- 可変の静的データと、禁止した関数を使わない（`tests/checks/CheckObjects.cmake`）。各ファイルに著作権表示を付ける。
- 再帰を使わない。確保はすべて、設定の確保関数からアリーナを通す。
- 単体テストは `tests/unit/test_<module>.c`。`amark_add_unit_test` で登録する。
- デコードは、設定によらず `amark_utf8_decode_lossy` を使う（`definition.c` の流儀）。正しい UTF-8 では結果が同じなので、要件 15.7 を自然に満たす。

## 2. 要件と既存の部品の対応

| 要件 | 既存の部品 | 差 |
|---|---|---|
| 1 解析の入口への統合 | `parse_document`、内容の範囲の一覧 | 呼び出しを足すだけ。インラインの解析の本体は **Missing** |
| 2 ノードの範囲 | `amark_tree_*`、`tree_check` | 範囲の決まりの実装は Missing。検査は既存のもので足りる |
| 3 テキスト | `amark_tree_set_range`、`amark_tree_remove` | テキストをまとめる方法が、ノードの数え方に響く（差 2、**Constraint**） |
| 4 エスケープ | なし | Missing。ASCII の句読点の判定が `refdef.c` と `definition.c` に `static` で重複している（差 5） |
| 5 文字参照 | `amark_entity_lookup` | 構文の認識は Missing。実体名の最大の長さの定数が、外から見えない（差 6、Constraint） |
| 6 コードスパン | 行の記録 | Missing。行をまたいで読む共通のカーソルがない（差 1） |
| 7 強調 | `amark_unicode_*`、`amark_tree_wrap` | 区切りのスタックは Missing。区切りの直前の文字を読む手段がない（差 4、**Constraint**） |
| 8 インラインの形式のリンク | `refdef.c` の `read_destination`、`read_title` | 同じ規則だが `static` で、定義に合わせた作り（差 3、Constraint） |
| 9 参照の解決 | `amark_label_scan`、`amark_definitions_find` | そのまま使える。括弧のスタックは Missing |
| 10 画像 | 同上 | Missing（リンクと同じ仕組み） |
| 11 オートリンク | なし | Missing |
| 12 生の HTML | `scan.c` の `scan_open_tag`、`scan_closing_tag` | `static` で、1 行の中だけを読む。インラインのタグは行をまたげる（差 3、Constraint） |
| 13 改行 | 行の記録 | Missing |
| 14 優先の順 | なし | Missing（走査の順で決まる） |
| 15 行と文字 | 行の記録、`amark_utf8_decode_lossy` | 差 1 と差 4 |
| 16 資源の上限 | `amark_document_node_count`、`amark_resolved_config_t` | 深さを知る手段と、包むときの深さの検査がない（差 2、**Constraint**） |
| 17 方言の箇所 | `amark_resolved_config_t.dialect`（今は読まれない） | 起動文字の表と区切りの種類の表は Missing |
| 18 共通の性質 | アリーナ、検査のスクリプト | 線形に保つ工夫は Missing（差 7、**Unknown**） |
| 19 単体テスト | `tree_dump`、`test_spec_*.c` の形 | インラインを書き出す手段がない。既存のテストの一部が変わる（差 8、差 9） |

## 3. 主な差

### 差 1：行をまたいで読む共通のカーソルがない（Missing）

インラインの構造の多くは行をまたぐ（コードスパン、生の HTML、title、強調、リンク）。行の境目では、行末記号、プレフィックス、先頭の空白を飛ばして、次の行の `content_start` へ進む。この進み方は、`refdef.c`（`refdef_cursor_t`、`next_line`）と `definition.c`（`label_cursor_t`、`cursor_skip_line_ending`）が、それぞれ `static` で持っている。インラインの解析が 3 つ目を持つと、同じ決まりが 3 か所になる。

- 選択肢：共通のカーソルのモジュールを作り、既存の 2 つも移す／インラインの解析の中だけに新しく作る／`refdef.c` のものを内部ヘッダに出す。
- 段落の続きの行は先頭の空白が除かれているので、`pad`（タブの残りの桁）は、内容の範囲の中では 0 のはずである。設計で確かめる。

### 差 2：ノードの数と深さの数え方（Constraint）

- `amark_document_node_count` は、取り除いたノードも数える。`block.c` は、ノードを足す前にこの数を上限と比べる。要件 16.3 は同じ数え方を求める。
- cmark の方式をそのまま移すと、区切りをまずテキストのノードにし、対応が決まったら取り除く（syntax-tree の設計も「区切りのテキストを `amark_tree_remove` で外し、間を `amark_tree_wrap` で包む」と想定している）。テキストをまとめるとき（要件 3.3）も、ノードを取り除く。すると、最終の木が小さくても、上限に早く達する。配列の要素も再利用されないので、メモリも増える。
- 深さ：`amark_inline_content_t` は深さを持たない。`block.c` は深さを自分のスタックの添字で知っていて、文書には残さない。インラインの解析は、段落の深さを知る手段が要る。
- `amark_tree_wrap` は、包まれる側の子孫をすべて 1 段深くする。内側の強調が先に閉じ、外側が後から包むので、ノードを足す時点の深さだけを調べても、上限の検査にならない。

選択肢：

| | やり方 | 良い点 | 気になる点 |
|---|---|---|---|
| (a) | cmark と同じく、木の上で作って直す | syntax-tree の想定どおり。実装が参照実装に近い | 取り除いたノードが数に入る。深さの検査に、部分木の高さの記録が要る |
| (b) | 作業用アリーナに字句と区切りの列を作り、対応が決まってから、文書の順に木へ書き出す | 取り除くノードがない。ノードの ID が文書の順になる。深さは書き出すときに分かる | 作業用の構造が 1 つ増える。`amark_tree_wrap` を使わない |
| (c) | (a) を基本にし、区切りだけはノードにせず、対応しなかった分だけをテキストにする | 取り除くノードが減る | テキストの挿入位置の管理が要る（`amark_tree_insert_before`） |

段落の深さは、`amark_inline_content_t` に深さを足す（block-parser の小さな変更）か、親をたどって数える（1 つの段落につき深さに比例する時間）かを選ぶ。

### 差 3：dest・title・タグの走査の再利用（Constraint）

- `refdef.c` の `read_destination` と `read_title` は、規則がインラインのリンクとほぼ同じである（山括弧、エスケープ、丸括弧を数える方式、行をまたぐ title）。違いは、インラインでは dest が空でもよいこと（`[a]()`）と、終わりが `)` で決まること。
- `scan.c` の `scan_open_tag`、`scan_closing_tag`、`scan_attribute_*` は、HTML ブロックの開始条件 7 のためのもので、1 行の `(p, len)` だけを読む。インラインのタグは、属性の間で改行できる。コメント、処理命令、宣言、CDATA の終わりを探す `amark_scan_html_end` も 1 行ずつである。
- 選択肢：既存の関数をカーソルを受け取る形に直して共有する／インライン用に書き、規則の重複を受け入れる／下位の文字の判定だけを共有する。既存の関数を直すと、block-parser のテスト（`test_refdef.c`、`test_scan.c`）に響く。

### 差 4：区切りの直前の文字（Constraint）

強調の判定には、区切りの列の直前と直後の文字が要る。`utf8.h` には、前へ進む向きのデコードしかない。「置き換えて続ける」設定では、不正なバイト列の極大の部分を後ろから正しく切り出すのは難しい。

- 選択肢：走査しながら直前のコードポイントを覚えておく／後ろ向きのデコードを `utf8` に足す（project-foundation の変更になる）。
- 行の先頭では、直前の文字は行末記号（空白）である（要件 7.2）。エスケープと文字参照の直後では、直前の文字が何になるかを、参照実装に合わせて決める必要がある（Research Needed）。
- U+FFFD は General Category が So なので、P+S の判定では句読点になる。U+0000 と不正なバイト列は句読点として働く。意図どおりかを、設計で確かめる。

### 差 5：文字の判定の重複（Missing、小）

ASCII の句読点、空白、行末記号の判定が、`refdef.c`、`definition.c`、`scan.c` に `static` で別々にある。インラインの解析は、これに加えて起動文字の表を持つ。共通の内部ヘッダにまとめるかどうかは、設計で決める。

### 差 6：実体名の長さの上限（Constraint、小）

`&` から `;` を探す範囲を限らないと、多数の `&` で時間が 2 乗になりうる。実体名の最大の長さは `entity_table.h` の中にあり、`entity.h` からは見えない。定数を出す（unicode-tables の小さな変更）か、生成スクリプトの出力に定数を足すかを選ぶ。数値の文字参照は、桁の上限（7 と 6）で自然に限られる。

### 差 7：時間を線形に保つ工夫（Unknown）

要件 18.1 が挙げる入力のそれぞれに、対策が要る。参照実装の対策を確かめて、設計に書く。

- 区切り：`openers_bottom`（種類、長さを 3 で割った余り、開閉の別ごとの下限）。
- コードスパン：バッククォートの列の長さごとに、最後に見た位置を覚える。長さの種類が多い場合の表の大きさ。
- 括弧：対応する `[` がないときの打ち切り。リンクができたときに、前の `[` を無効にする処理。
- 閉じない `<!--`、`<?`、`<!X`、`<![CDATA[`：種類ごとに「この先に終わりがない」ことを覚える。
- ラベル：`amark_label_scan` は 1 回に最大 1,000 文字を読む。`]` のたびに呼ぶと、時間は線形だが係数が大きい。受け入れられるかを確かめる。
- 定義の表の検索は、1 回につき log N とラベルの長さの積である。

### 差 8：インラインのテストの基盤（Missing）

- `amark_test_dump_blocks` は、インラインを書き出さない。インラインの種類、入れ子、属性を比べる手段が要る。範囲も書き出せると、要件 19.2 を表で確かめられる。
- spec.txt はリポジトリにない。ブロックの例は、入力と期待する文字列を `test_spec_*.c` の表に手で書いている（著作権表示つき）。インラインの 12 節は約 330 個の例があり、期待する構造を HTML から書き起こす量が多い。
- 選択肢：ブロックと同じく手で表にする／HTML から期待する文字列を作る補助のスクリプトを `tools/` に置き、生成物をコミットする。spec-conformance は、後で spec.txt の全例を HTML と比べるので、重複の度合いも考える。

### 差 9：既存のテストへの影響（Constraint）

インラインのノードが加わると、`amark_parse` を通す既存のテストのうち、ノードの数を確かめるものが変わる。

- `tests/unit/test_block_limits.c`：`parse_ok` が `amark_document_node_count` を期待値と比べる。ノードの数の上限の境の値も、ブロックだけの数で書かれている。
- `tests/unit/test_parse.c`：数を比べるのは、同じ入力どうしなので変わらない。「段落 2 つで 3 ノード」のような注釈は古くなる。
- `amark_test_dump_blocks` はインラインを書かないので、`test_spec_*.c` と `test_block.c`（`amark_block_parse` を直接呼ぶ）は変わらない。
- 要件 19.11 は、ブロックの構造が変わらないことを求める。数を比べるテストは、期待値を直すか、`amark_block_parse` を直接呼ぶ形に変えるかを選ぶ。

## 4. 実装の進め方の選択肢

### Option A：既存のモジュールを広げる

`block.c` の段落を閉じる処理にインラインの解析を足し、`scan.c` と `refdef.c` にインラインの走査を足す。

- ✅ ファイルが増えない。
- ❌ `block.c` はすでに 1,626 行ある。責任が混ざる。
- ❌ 定義の表ができる前（ブロックの解析の途中）には、参照を解決できない。2 段階の構成（steering の決定）に反する。

採れない選択肢に近い。

### Option B：新しいモジュールだけで作る

`inline.c`（と必要なら字句、区切り、括弧のファイル）を新しく作り、既存のファイルは `parse.c` の 1 行と `CMakeLists.txt` だけを変える。dest、title、タグ、カーソルは、インライン用に書く。

- ✅ block-parser のコードとテストに触れない。レビューの範囲がはっきりする。
- ❌ 行をまたぐカーソル、dest と title、タグの規則が、2 か所か 3 か所に重複する。規則を直すときに、片方を忘れやすい。

### Option C：新しいモジュールを中心に、上流を最小限だけ広げる

インラインの解析は新しいモジュールにし、共有する価値のある部品だけを上流から取り出す。候補は次のとおり。

- 行をまたぐカーソル（差 1）
- dest と title の走査（差 3）
- 文字の判定（差 5）
- 実体名の最大の長さの定数（差 6）
- 内容の範囲の一覧に深さを足す（差 2）

- ✅ 規則が 1 か所にまとまる。source-text も同じカーソルを使える見込みがある。
- ❌ 承認済みの block-parser と unicode-tables の実装を触る。既存のテストを通し直す必要がある。
- ❌ どこまで取り出すかの線引きを、設計で決めなければならない。

block-parser も、同じ形（新しいモジュールを中心に、上流を最小限だけ広げる）を選んでいる。

## 5. 規模とリスク

- **規模：L（1〜2 週間）**。要素が 12 種類あり、強調とリンクの規則が込み入っている。spec.txt の約 330 個の例と、病的な入力のテストも要る。使う部品はそろっていて、構成を変える必要はない。
- **リスク：Medium**。方式は cmark に手本がある。ただし、範囲だけを持つ木、取り除いたノードも数える上限、包むときの深さ、行をまたぐ読み方は、参照実装にない条件で、設計で詰める点が多い。

## 6. 設計への申し送り

### 決めること

1. 木を作る方式（差 2 の (a)(b)(c)）。ノードの数の上限の意味と、ノードの ID の順に響く。
2. 深さの上限の検査の方法と、段落の深さの渡し方。
3. 上流から取り出す部品の線引き（Option B か C か、C ならどれを取り出すか）。
4. 直前の文字の扱い（差 4）。
5. モジュールの分け方。brief の境界の候補は、字句の走査、区切りの処理、括弧の処理の 3 つである。
6. 方言の箇所の形（起動文字の表と、区切りの種類の表）。block-parser が確保した箇所の形に合わせる。
7. テストの書き出しの形と、spec.txt の例の持ち方（差 8）。既存のテストの直し方（差 9）。

### 要件の見直しの候補

- **16.3（取り除いたノードも数える）**：方式 (a) では、利用者から見て、上限が最終の木の大きさより厳しく働く。方式を決めた後で、文言が実態に合うかを見直す。
- **3.3（テキストをまとめる）と 2.3（内容を隙間なく覆う）**：実装の費用が方式によって変わる。要件は変えずに済む見込みだが、設計で確かめる。
- **15.4、15.5（U+FFFD として判定する）**：U+FFFD は句読点として働く（差 4）。参照実装の動作と比べて、文言を足すかを決める。

### Research Needed

- cmark の `inlines.c` の各対策（`openers_bottom`、バッククォートの位置の表、括弧のスタックの無効化、閉じない HTML の構造の打ち切り）と、0.31.2 での変更点。
- エスケープと文字参照の直後にある区切りの列の、直前の文字の扱い（cmark と commonmark.js で同じか）。
- 行末の空白の扱い（タブを含む場合）が、cmark と commonmark.js で違う点。要件 3.5 と 13.4 は、スペースとタブの両方を改行のノードに含める。
- `amark_label_scan` を `]` のたびに呼ぶときの時間（最大 1,000 文字 × 回数）を、病的な入力で測る。
- インラインの dest の丸括弧の入れ子に、参照実装は上限（32）を置く。要件 8.6 は上限を置かない。spec.txt の例と食い違わないかを確かめる。
- cmark の pathological tests の一覧と、要件 18.1 の入力の対応。

---

# Research & Design Decisions

## Summary
- **Feature**: `inline-parser`
- **Discovery Scope**: Complex Integration（新しい解析の段階を、既存の入口・木・行の記録・定義の表につなぐ）
- **Key Findings**:
  - cmark の方式（区切りのスタックと括弧のスタック）は、作業用の「項目の木」の上でそのまま動かせる。木には、決まったノードだけを文書の順に足せばよい。これで、取り除いたノードが上限に数えられる問題と、後から包むと深くなる問題が、どちらも消える。
  - 山括弧のない dest の丸括弧に上限がないと、`[a](b` の繰り返しで時間が 2 乗になる。cmark は 32 段で打ち切る。仕様も上限を認めている。要件 8.6 を直した。
  - cmark の `no_link_openers` の印は、新しい `[` が来ると戻るので、`[a [b](c) [d] e](f)` で外側がリンクになってしまう。括弧のスタックの高さで「リンクの下限」を持てば、たどらずに正しく無効にできる。
  - 上流への追加は `amark_array_clear` の 1 つで足りる。後ろ向きのデコード、実体名の長さの定数、内容の一覧の深さは、足さずに済む。

## Research Log

### cmark の区切りの処理
- **Context**: 強調の対応を、線形の時間で、仕様の 17 の規則どおりに決める方法。
- **Sources Consulted**: cmark 0.31.1 `src/inlines.c`（`scan_delims`、`process_emphasis`、`S_insert_emph`）、CommonMark 0.31.2 付録「A parsing strategy」
- **Findings**:
  - 区切りの列の直前の文字は、原文の直前のバイトから読む。エスケープや文字参照の直後でも、原文の文字（`\*` なら `*`、`&amp;` なら `;`）を見る。内容の先頭と、デコードできないときは、改行（空白）として扱う。
  - `openers_bottom` は、`_` と `*` のそれぞれに「開けるかどうか × 長さを 3 で割った余り」の 6 個を持つ。
  - 使う区切りは、開く側と閉じる側の長さがともに 2 以上なら 2 つ、そうでなければ 1 つ。開く列は内側から使う。
  - 開く区切りが見つからず、閉じる区切りが開けないものなら、スタックから外す。
- **Implications**: Delimiter の `process` の契約を、この形にした。下限の表は、種類 1 つにつき 6 個とし、種類の数で大きさを決める（方言の区切りを足せる）。直前の文字は、行の内容の始まりでは U+000A とする（cmark は行のプレフィックスを除いた写しの上で動くので、同じ結果になる）。

### cmark の括弧の処理と、リンクの中のリンク
- **Context**: `]` でリンクか画像を作るかを決める順と、リンクの中のリンクを禁じる方法。
- **Sources Consulted**: `handle_close_bracket`、`push_bracket`、`link_label`
- **Findings**:
  - 順は、インライン、続くラベル、テキストをラベルとする参照である。続くラベルが見つかって定義がないときは、ショートカットを試さない。
  - `bracket_after`（テキストの中に括弧がある）のときは、テキストをラベルにしない。
  - リンクができると `no_link_openers` を立て、次のリンクの `[` で倒す。倒した後は、古い `[` もまた有効に見える。
  - 続くラベルが空白だけ（`[ ]`）のとき、cmark は省略参照として扱い、commonmark.js はラベルを引いて失敗する。仕様の文言では、`[ ]` はラベルでも `[]` でもない。
- **Implications**: 「テキストがラベルになれる」は、`amark_label_scan` の結果の終わりが `]` と一致することで判定する（`bracket_after` と同じ効果になる）。リンクの下限は、スタックの高さで持つ。`[a][ ]` は、仕様の文言に従い、ショートカットのリンクと、テキスト ` [ ]` にする。参照実装との違いなので、テストに書く。

### 線形に保つ工夫
- **Context**: 要件 18.1 の入力のそれぞれで、時間が 2 乗にならないこと。
- **Sources Consulted**: `scan_to_closing_backticks`、`handle_pointy_brace`、`manual_scan_link_url_2`、`test/pathological_tests.py`
- **Findings**:
  - バッククォート：列の長さ（1,000 まで）ごとに最後に見た位置を持ち、終わりまで探したら印を立てる。1,000 を超える列は、コードスパンにしない。
  - 閉じない `<!--`、`<?`、`<!X`、`<![CDATA[`：種類ごとの印で、2 回目から探さない。
  - dest：丸括弧が 32 を超えたら打ち切る。
  - 病的な入力の一覧は、設計の「Performance & Scalability」の表に対応させた。
- **Implications**: バッククォートの表は、長さを添字にして伸ばす配列にし、列の長さに上限を置かない（要件 6.1）。位置を内容の先頭からのオフセットで持つので、内容が変わっても空にしなくてよい。文字参照は、英数字の並びだけを読むので、実体名の最大の長さを知らなくても線形である。

### 行末の空白と改行
- **Context**: 改行のノードの範囲と、ハードとソフトの判定。
- **Sources Consulted**: `handle_newline`、`parse_inline` の既定の枝（`cmark_chunk_rtrim`）
- **Findings**: cmark は、行末のテキストから空白（タブを含む）を除く。ハードの判定は、行末記号の直前の 2 バイトがスペースかどうかである。次の行の先頭のスペースは飛ばす。commonmark.js は、行末のスペースだけを除く。
- **Implications**: 要件 3.5 と 13.4（スペースとタブを改行のノードに含める）は、cmark の動作と合う。spec.txt に、行末のタブの例はない。

### 既存のテストへの影響
- **Context**: インラインのノードが加わると、`amark_parse` を通す既存のテストがどう変わるか。
- **Findings**: `tests/unit/test_block_limits.c` は、深さとノードの数の境の値を、ブロックだけの数で書いている（`parse_ok` と `parse_exceeds` が 72 か所）。段落のテキストは、段落より 1 段深く、ノードを 1 つ増やす。`tests/public/test_parse.c` は、文書の順のノードの表を持つ。`test_block.c` は `amark_block_parse` を直接呼ぶので変わらない。
- **Implications**: 期待値を直す作業を、設計の「Modified Files」に入れた。

## Architecture Pattern Evaluation

| Option | Description | Strengths | Risks / Limitations | Notes |
|--------|-------------|-----------|---------------------|-------|
| 木の上で作って直す | cmark と同じく、区切りをテキストのノードにし、後で取り除いて包む | 参照実装に近い。syntax-tree の想定どおり | 取り除いたノードが上限に数えられる。包むと深さが変わる。ノードの配列にごみが残る | 採らない |
| 項目の木を作ってから書き出す | 作業用の項目で対応を決め、決まったものだけを木へ足す | 上限が最終の木と一致する。ID が文書の順。木を作る API は `append` だけ | 作業用の木の操作（包む、取り除く）を、この spec が持つ | **採る** |
| 区切りだけを遅らせる | 区切りはノードにせず、残った分だけを後から挿入する | 取り除くノードが減る | テキストをまとめるときに、やはり取り除く。挿入位置の管理が要る | 採らない |

## Design Decisions

### Decision: 項目の木を作ってから書き出す
- **Context**: `amark_document_node_count` は取り除いたノードも数える。`amark_tree_wrap` は子孫を深くする。要件 16.3 と 16.4 は、上限を利用者に分かる形で守ることを求める。
- **Alternatives Considered**: 上の表の 3 つ。
- **Selected Approach**: 内容ごとに、作業用のアリーナの配列に項目の木を作る。区切りと括弧の対応が決まった後で、文書の順にたどって `amark_tree_append` する。
- **Rationale**: ノードの数は最終の木の大きさと一致し、深さは書き出すときに分かる。失敗しても、木には決まったノードしかない。
- **Trade-offs**: 項目の木の操作を自分で持つ（200 行ほどの見込み）。`amark_tree_wrap` と `amark_tree_remove` は、この spec では使わない（dialect-extension の後処理が使う見込みがあるので、残す）。
- **Follow-up**: syntax-tree の設計の注記（inline-parser が `amark_tree_remove` と `amark_tree_wrap` を使う）を直す。

### Decision: 上流の走査は作り直さず、インライン用に書く
- **Context**: 行をまたぐカーソル、dest と title、タグの走査が、`refdef.c`、`definition.c`、`scan.c` に `static` である（Gap Analysis の差 1 と差 3）。
- **Alternatives Considered**:
  1. 共通の部品に取り出し、既存の 3 つのファイルも移す
  2. インライン用に新しく書く
- **Selected Approach**: 2。`cursor` と `inline_scan` を新しく書く。
- **Rationale**: 既存の関数は、定義と HTML ブロックに合わせた前提（開始が行の内容の始まり、1 行だけ、dest は空でない）を持ち、そのままでは使えない。承認済みのコードとテストを動かさずに済む。
- **Trade-offs**: 規則が 2 か所になる。同じ例を両方のテストに通して、ずれを見つける。
- **Follow-up**: source-text もカーソルを要する見込みである。そのときに、3 つをまとめるかを改めて決める。

### Decision: 直前の文字は、その場で後ろへ読む
- **Context**: 強調の判定に、区切りの列の直前の文字が要る。`utf8` には、前へ進む向きのデコードしかない（差 4）。
- **Selected Approach**: 最大 3 バイトの継続バイトを戻り、そこから `amark_utf8_decode_lossy` で読む。読んだ長さが元の位置にちょうど届けばその文字、届かなければ U+FFFD とする。
- **Rationale**: 正しい列の先頭バイトは、前の不正な列に取り込まれない（継続バイトの範囲と先頭バイトの範囲が重ならない）ので、前から読んだ場合と同じ文字になる。届かないときは、直前のバイトは不正な列の一部であり、U+FFFD である。
- **Trade-offs**: U+FFFD は General Category が So なので、不正なバイト列と U+0000 は句読点として働く。cmark も U+0000 を U+FFFD に置き換えてから解析するので、同じである。

### Decision: 段落の深さは、親をたどって数える
- **Context**: 内容の一覧（`amark_inline_content_t`）は深さを持たない。
- **Alternatives Considered**: 一覧に深さを足す（block-parser の変更）／親をたどる。
- **Selected Approach**: 深さの上限が指定されているときだけ、`amark_node_parent` で根までたどる。
- **Rationale**: 深さ d の段落が始まる行は、d に比例するプレフィックスを持つので、合計は内容の長さの定数倍である。block-parser を変えずに済む。

### Decision: 要件 8.6 を直した
- **Context**: 要件 8.6 は、dest の丸括弧の対応を「入れ子の深さによらず判定する」としていた。
- **Selected Approach**: 「入れ子が 32 段まで」に直した。
- **Rationale**: 上限がないと、`[a](b` の繰り返しで、`]` のたびに内容の終わりまで読むことになり、時間が 2 乗になる（要件 18.1 と両立しない）。仕様は「実装は上限を置いてよい。3 段以上は扱うべき」としている。32 は cmark と同じ値である。
- **Trade-offs**: 33 段以上の丸括弧を含む dest は、山括弧で囲むか、エスケープしなければリンクにならない。

### Decision: テストの書き出しは、原文を添える
- **Context**: spec.txt の 356 個の例で、インラインの構造を比べる手段（差 8）。
- **Selected Approach**: `amark_test_dump_tree` は、子を持たない種類について、範囲の原文を引用符で添える。リンクは、dest と title の原文、参照先の定義のラベルを添える。
- **Rationale**: 期待する文字列を、仕様の HTML から手で書き起こせる。テキストの区切り方と範囲の誤りが、文字列の違いとして見える。範囲の数値は、別のテストで確かめる。

### Synthesis
- **Generalization**: `*` と `_` は、区切りの種類の表の 2 つの要素として扱う。リンクと画像は、括弧のスタックの印 1 つの違いにする。4 つの「閉じない HTML」は、1 つのビットの集まりで扱う。方言の区切りと起動文字は、同じ表に足すだけで流れに乗る（実装はこの spec の範囲に留める）。
- **Build vs. Adopt**: 規則と線形に保つ工夫は、cmark の方式を採る（コードは写さない）。木を作る部分は、作業用の項目の木を自分で作る。上流の `amark_label_scan` と `amark_definitions_find` は、そのまま使う。
- **Simplification**: 共通のカーソルへの作り直し、内容の一覧への深さの追加、後ろ向きのデコード、実体名の長さの定数は、要らないと分かったので外した。モジュールは 6 つで、木に触れるのは 1 つの関数（`amark_draft_emit`）だけである。ファイルの名前は、既存のもの（`block`、`scan`、`refdef`、`definition`、`line`）にならって 1 語にした（`cursor`、`draft`、`delimiter`、`bracket`）。

## Risks & Mitigations
- 強調とリンクの規則の境の場合を取りこぼす — spec.txt の 356 個の例を、節ごとの表で確かめる。
- 規則が `refdef.c`・`scan.c` と 2 か所になり、ずれる — 同じ dest、title、タグの例を、両方のテストに通す。
- 既存のテストの期待値の直しに、誤りが混ざる — ブロックの構造を比べるテスト（`amark_test_dump_blocks`）は変えずに通ることで、ブロックの木が変わっていないことを確かめる。
- 病的な入力のテストが遅い環境で時間切れになる — 入力の大きさを、線形なら 1 秒より十分に短く、2 乗なら制限時間を大きく超える値にする。
- 参照実装との違い（`[a][ ]`、行末のタブ、1,000 個を超えるバッククォートの列）が、spec-conformance で問題になる — spec.txt に該当する例はない。違いをテストに明記する。

## References
- [CommonMark Spec 0.31.2](https://spec.commonmark.org/0.31.2/) — 6 章と付録「A parsing strategy」
- [cmark 0.31.1 `src/inlines.c`](https://github.com/commonmark/cmark/blob/0.31.1/src/inlines.c) — 区切りと括弧の処理、線形に保つ工夫
- [cmark 0.31.1 `test/pathological_tests.py`](https://github.com/commonmark/cmark/blob/0.31.1/test/pathological_tests.py) — 病的な入力の一覧
