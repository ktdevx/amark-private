# 表の入力データ

このディレクトリには、`tools/gen_tables.py` が `src/unicode_tables.h` と `src/entity_table.h` を作るときに読む入力データを置く。どのファイルも、取得元から取得したままの内容である。内容も行末も変えない（リポジトリの `.gitattributes` で、このディレクトリの行末の変換を止めている）。

ファイルの同一性は、次の SHA-256 で確かめられる。

```sh
sha256sum tools/data/DerivedGeneralCategory.txt tools/data/CaseFolding.txt tools/data/entities.json
```

## DerivedGeneralCategory.txt

| 項目 | 内容 |
|------|------|
| 取得元 | <https://www.unicode.org/Public/18.0.0/ucd/extracted/DerivedGeneralCategory.txt> |
| 版 | Unicode 18.0.0（1 行目 `# DerivedGeneralCategory-18.0.0.txt`） |
| 大きさ | 282,143 バイト |
| SHA-256 | `d6b151d2d40ee9b1876d26f417980f45ffae47b6055ccf7203cb31f07a030f94` |
| 利用の条件 | Unicode License v3（SPDX: `Unicode-3.0`、<https://www.unicode.org/license.txt>）。Copyright © 1991-2026 Unicode, Inc. |

## CaseFolding.txt

| 項目 | 内容 |
|------|------|
| 取得元 | <https://www.unicode.org/Public/18.0.0/ucd/CaseFolding.txt> |
| 版 | Unicode 18.0.0（1 行目 `# CaseFolding-18.0.0.txt`） |
| 大きさ | 88,776 バイト |
| SHA-256 | `a004797658a457bec4dc11683e39f69249ea3b595b752dbea6721c4c9f587b0d` |
| 利用の条件 | Unicode License v3（SPDX: `Unicode-3.0`、<https://www.unicode.org/license.txt>）。Copyright © 1991-2026 Unicode, Inc. |

UCD の 2 ファイルは、同じ版（18.0.0）でなければならない。ファイルの名前からは版を除いてあり、版は各ファイルの 1 行目で分かる。

## entities.json

| 項目 | 内容 |
|------|------|
| 取得元 | <https://html.spec.whatwg.org/entities.json>（WHATWG の HTML Standard） |
| 版 | 版の番号はない。2026-11-23 に取得した（サーバーの `Last-Modified` は 2025-11-12 00:20:03 GMT） |
| 大きさ | 145,897 バイト |
| SHA-256 | `d741d877ac77c4194c4ad526b5b4a19aef8dfe411ab840a466891cdbb9f362e6` |
| 利用の条件 | Copyright © WHATWG (Apple, Google, Mozilla, Microsoft)。このファイル（複製）は Creative Commons Attribution 4.0 International（CC BY 4.0、<https://creativecommons.org/licenses/by/4.0/>）に従う。ソースコードに取り込んだ部分（生成した `src/entity_table.h`）は、代わりに BSD 3-Clause License に従う |

entities.json は版を持たず、同じ URL の内容が変わりうる。生成物の先頭には、このファイルの SHA-256 を版として書く。

## 版を上げるとき

ファイルを差し替えたら、この文書の取得元、版（entities.json は取得した日）、大きさ、SHA-256 を直す。
