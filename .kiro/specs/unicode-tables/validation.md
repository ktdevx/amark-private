# 検証の記録：unicode-tables（タスク 5.1）

- 日付：2026-11-23
- 対象：ブランチ `ccr-15351edc-0mf6av` の作業ツリー（HEAD `dfc027a`、未コミットの変更なし）
- 比較の基準：`8163116`（この仕様の作業を始める前のコミット）
- 対象の要件：1.2、1.5、1.8、3.1、3.3、8.4、8.5、8.9、8.10
- ビルドのディレクトリは、すべてリポジトリの外（作業用の一時ディレクトリの `v51/` の下）に置いた

## 手元の環境

| 道具 | 版 |
|---|---|
| OS | Linux 6.18.44（Ubuntu 24.04 の userland） |
| GCC | 13.3.0（Ubuntu 13.3.0-6ubuntu2~24.04.1） |
| Clang | 18.1.3（Ubuntu clang 18.1.3-1ubuntu1） |
| CMake | 3.28.3 |
| Python | 3.13.18 |
| GNU ld | 2.42 |

## 1. 構成ごとのビルドとテスト

すべての構成で、次のコマンドを実行した（`$B` は構成ごとのビルドのディレクトリ）。

```sh
cmake -S . -B $B -DAMARK_WARNINGS_AS_ERRORS=ON -DBUILD_SHARED_LIBS=<OFF|ON> \
      -DCMAKE_BUILD_TYPE=<Debug|Release> -DCMAKE_C_COMPILER=<gcc|clang> -DCMAKE_CXX_COMPILER=<g++|clang++>
cmake --build $B -j8
ctest --test-dir $B -j8 --output-on-failure
```

| コンパイラ | ライブラリ | 構成 | 結果 |
|---|---|---|---|
| GCC | 静的 | Debug | 171 / 171 通過 |
| GCC | 静的 | Release | 171 / 171 通過 |
| GCC | 動的 | Debug | 172 / 172 通過 |
| GCC | 動的 | Release | 172 / 172 通過 |
| Clang | 静的 | Debug | 171 / 171 通過 |
| Clang | 静的 | Release | 171 / 171 通過 |
| Clang | 動的 | Debug | 172 / 172 通過 |
| Clang | 動的 | Release | 172 / 172 通過 |

- 警告のエラー化を有効にした状態で、8 つの構成のすべてでビルドが通った（`-Woverlength-strings` を含め、警告は出ていない）。
- 動的の構成が 1 つ多いのは、`checks.exports` が動的ライブラリの構成でだけ登録されるため。
- 各構成で、次のテストが登録され、通った：`test_unicode` の 10 件（全コードポイントでの句読点・空白・畳み込みの一致、2 つの判定が同時に真にならないこと、例の検査）、`test_entity` の 9 件（全実体名の一致、表の前後の名前、長さ 0 など）、`checks.objects`、`checks.license_headers`、`checks.generated_tables`、`checks.generator`、（動的のみ）`checks.exports`。基準のコミットでの 150 件に、この仕様で足したテストが加わっている（要件 8.10）。

### ASan と UBSan

```sh
cmake -S . -B $B -DAMARK_WARNINGS_AS_ERRORS=ON -DCMAKE_BUILD_TYPE=Debug -DAMARK_SANITIZE=ON \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build $B -j8
ctest --test-dir $B -j8 --output-on-failure
```

| コンパイラ | 結果 |
|---|---|
| GCC（ASan + UBSan、Debug、静的） | 170 / 170 通過。`runtime error` と `AddressSanitizer` の出力なし |
| Clang（ASan + UBSan、Debug、静的） | リンクで失敗（環境の制約。下を参照） |

- サニタイザの構成では、`tests/CMakeLists.txt` の条件（`NOT AMARK_SANITIZE`）により `checks.objects` が登録されない。そのため件数は 170 件。`checks.objects` は上の 8 構成で通っている。

### 環境の制約：Clang の ASan

この環境の Clang 18 には、ASan の実行時ライブラリがない。リンクは次のエラーで失敗する。

```
/usr/bin/ld: cannot find /usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.asan_static-x86_64.a: No such file or directory
/usr/bin/ld: cannot find /usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.asan-x86_64.a: No such file or directory
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```

基準のコミット `8163116` のソース（`git archive 8163116 | tar -x` で一時ディレクトリに展開したもの）でも、同じコマンドで同じエラーになる。この仕様の変更による失敗ではない。Clang の ASan と UBSan は、CI の `sanitize` のジョブ（Linux Clang (ASan, UBSan)）で確かめる。

## 2. Python を見つけられない構成（要件 1.8）

```sh
cmake -S . -B $B -DAMARK_WARNINGS_AS_ERRORS=ON -DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON \
      -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build $B -j8
ctest --test-dir $B -j8 --output-on-failure
```

- 結果：ビルドが通り、169 / 169 のテストが通った。
- `ctest -N` の一覧で、`checks.generated_tables` と `checks.generator` が登録されていないことを確かめた（`checks.objects` と `checks.license_headers` は登録されている）。コミットされた生成物をそのままコンパイルし、ライブラリとテストのビルドに Python を必要としない。

## 3. リポジトリの検査（要件 1.5、8.4、8.5、8.9）

| 確かめたこと | 方法 | 結果 |
|---|---|---|
| 動的ライブラリの公開シンボルが変わっていない（8.9） | `checks.exports`（動的の 4 構成）と `git diff 8163116 -- tests/checks/exported_symbols.txt` | 4 構成とも通過。差分は空。`nm -D --defined-only` でも、`unicode`・`entity`・`fold`・`punct` を含む公開シンボルはない |
| 可変の静的データがない（8.4、8.5） | `checks.objects`（サニタイザ以外の 8 構成） | 8 構成とも通過。`nm libamark.a` で、`amark_entity_entries`、`amark_entity_names`、`amark_unicode_fold_simple_table`、`amark_unicode_fold_full_table` などの表が読み取り専用（`r`）に置かれていることも確かめた |
| 生成物を含むすべてのソースに著作権表示がある（1.5） | `checks.license_headers`（全構成） | 全構成で通過 |

## 4. 生成物が最新であることの検査（要件 1.2、3.3）

```sh
python3 -B tools/gen_tables.py --check
python3 -B tools/test_gen_tables.py
```

- `gen_tables.py --check`：終了コード 0（コミットされた生成物と、生成し直した結果がバイト単位で同じ）。
- `test_gen_tables.py`：22 件のテストがすべて通った（`Ran 22 tests ... OK`）。
- CTest からも、`checks.generated_tables` と `checks.generator` として全構成で通った。
- 検査の後の `git status --porcelain` は空。`__pycache__` はリポジトリの中にできていない（要件 3.4）。

## 5. インストールと利用側のプロジェクト（README の手順）

GCC の Release の構成から、README の手順でインストールし、`tests/consumer` をビルドしてテストした。

```sh
cmake --install $B --config Release --prefix $P
cmake -S tests/consumer -B $C -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$P"
cmake --build $C --config Release
ctest --test-dir $C -C Release --output-on-failure
```

| 構成 | 結果 |
|---|---|
| 静的のインストール + `find_package` | 1 / 1 通過 |
| 動的のインストール + `find_package` | 1 / 1 通過 |
| `-DAMARK_CONSUMER_MODE=subdirectory` | 1 / 1 通過 |

- インストール先に内部のヘッダ（`unicode.h`、`entity.h`、生成物）は置かれていない。置かれるのは `amark.h`、`export.h`、`version.h`、CMake のパッケージのファイル、ライブラリ、`share/licenses/amark/LICENSE` だけ。インストールした `LICENSE` に Unicode License v3 の表示が含まれている。

## 6. CI で確かめること（未確認）

ブランチ `ccr-15351edc-0mf6av` にはプルリクエストがなく、GitHub Actions のこのブランチの実行は 0 件である。`.github/workflows/ci.yml` は `main` への push と `pull_request` でだけ動くため、CI の結果はまだない。次の項目は **未確認** である。

- 要件 1.2：OS によらず、生成物がバイト単位で同じであること。手元（Linux）では `--check` が通るが、ほかの OS では確かめていない。
- 要件 3.1：Windows（MSVC と MinGW）と macOS で、生成物がコンパイルでき、一致の検査が通ること。
- Clang の ASan と UBSan での全テスト（上の環境の制約のため）。

プルリクエストを作った後（または `main` に入った後）、CI で次を確かめる。

1. `build` のマトリックスの全ジョブ（Linux GCC、Linux Clang、macOS Clang、Windows MSVC、Windows MinGW UCRT64 のそれぞれについて静的と動的）が通ること。特に、Windows MSVC で `test_unicode` と `test_entity` が `fopen` の警告（C4996）なしにビルドでき、`AMARK_TEST_DATA_DIR` のファイルが読めること、MSVC の文字列の長さの制限に当たらないこと。
2. 各ジョブの `ctest` の出力に、`checks.generated_tables` と `checks.generator` が実行されて通ったものとして出ていること（Windows、macOS、MinGW のランナーで Python 3.9 以上が見つからないと、2 つの検査は登録されず、黙って省かれる。省かれたジョブでは、要件 1.2 と 3.1 の一致の検査が行われていないことになる）。
3. `generated-tables` のジョブ（Generated tables）で、`gen_tables.py --check` と `test_gen_tables.py` が通ること。
4. `sanitize` のジョブ（Linux Clang (ASan, UBSan)）、`subproject` のジョブ、`min-cmake` のジョブ（CMake 3.21）が通ること。

## まとめ

- 手元で確かめた 8 つの構成（GCC・Clang × 静的・動的 × Debug・Release）、GCC の ASan + UBSan、Python を見つけられない構成のすべてで、ビルドが通り、`ctest` が全部通った。
- 公開シンボルの一覧、可変の静的データ、著作権表示は、既存の検査で問題がない。
- Clang の ASan は環境の制約でリンクできない（基準のコミットでも同じ）。
- OS によるバイト単位の一致（1.2）と、Windows・macOS での確認（3.1）は、CI が未実行のため未確認である。
