<!--
役割: Zipper Explorer の視覚調整に使う、Windows 11 File Explorer の測定記録。
依存する自プロジェクト内ファイル: なし。このファイルは記録専用であり、コードから読み込み・解析・参照してはならない。
-->

# Zipper Explorer: Windows 11 視覚測定記録

## 利用上の制約

この文書は実装用の設定ファイルではない。`ExplorerMetrics`、テーマ、UIコードはこの文書から値を取得しないこと。実装時には必要な値をレビューして手動で採用し、採用した値とこの測定記録を混同しない。

## 別 DPI 再測定（2026-08-25）

### 実施範囲と制約

この節は既存の **168 DPI / 175%** の測定を保持したまま、別 DPI での再測定を試みた記録である。コードは変更していない。

- `GetDpiForSystem()` は **96 DPI** を返した（`A-API`）。これはシステム DPI であり、個別の Explorer ウィンドウのレイアウト値ではない。
- 同じ実行コンテキストで Windows UI Automation のデスクトップ直下を列挙したが、トップレベル要素を取得できず、`CabinetWClass`（Explorer ウィンドウ）も取得できなかった。
- 一時的に Explorer を起動して再取得も試みたが、この実行コンテキストから対象ウィンドウの `BoundingRectangle` を得られなかった。
- 表示倍率の変更は既存の作業環境全体へ影響し、復帰時にサインアウト等を伴う可能性がある。そのため変更していない。

よって、**96 DPI の詳細な UI 実測は成立していない**。`GetDpiForSystem() = 96` のみを根拠に、各 UI 部品の寸法を作成・補完していない。

| 確認項目 | 結果 | 種別 | 備考 |
| --- | --- | --- | --- |
| システム DPI | 96 DPI | A-API | `GetDpiForSystem()`。Explorer 個別ウィンドウの DPI ではない。 |
| 仮想スクリーン | 1463 × 914 physical px | A-API | `GetSystemMetrics(SM_CXVIRTUALSCREEN/SM_CYVIRTUALSCREEN)`。寸法比較には未使用。 |
| Explorer の `GetDpiForWindow()` | N/A | N/A | 対象 HWND を UI Automation から取得できなかった。 |
| Explorer の UIA `BoundingRectangle` | N/A | N/A | デスクトップ UIA ツリーをこの実行コンテキストから列挙できなかった。 |
| スクリーンショット境界の別 DPI 測定 | N/A | N/A | 同一 DPI の実 Explorer 表示を安全に取得できなかった。 |

### 168 DPI 記録との比較

以下の「別 DPI」は上記理由により実測値ではない。差分欄を推測値で埋めず、比較不能であることを明示する。

| 対象 | 168 DPI physical px | 168 DPI logical px | 別 DPI physical px | 別 DPI logical px | 判定 | 根拠・扱い |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| Back / Forward / Up / Refresh hitbox | 56 × 56 | 32.0 × 32.0 | N/A | N/A | D | 別 DPI の Explorer UIA 矩形を取得できず、安定性を判定不能。 |
| Navigation button 間 gap | 28 | 16.0 | N/A | N/A | D | 同上。 |
| 行上端→Navigation button | 13 | 7.4 | N/A | N/A | D | 同上。 |
| Refresh→Address Bar gap | 25 | 14.3 | N/A | N/A | D | 同上。 |
| Address Bar 高さ | 56 | 32.0 | N/A | N/A | D | 同上。 |
| Address Bar→Search Box gap | 14 | 8.0 | N/A | N/A | D | 同上。 |
| Command Bar 高さ | 78 | 44.6 | N/A | N/A | D | 同上。 |
| icon-only Command Button 幅 | 70 | 40.0 | N/A | N/A | D | 同上。 |
| icon-only Command Button 間 gap | 14 | 8.0 | N/A | N/A | D | 同上。 |
| Command icon 外接サイズ | 28 × 28 | 16.0 × 16.0 | N/A | N/A | D | 同上。Glyph の実線サイズではなく UIA Image 矩形。 |
| Command Bar 上端→icon 上端 | 28 | 16.0 | N/A | N/A | D | 同上。 |
| Details Header 高さ | 48 | 27.4 | N/A | N/A | D | 同上。 |
| Command Bar 終端→Header 開始 | 4 | 2.3 | N/A | N/A | D | 同上。 |
| Details row 高さ | 43 | 24.6 | N/A | N/A | D | 同上。 |
| row pitch | 51 | 29.1 | N/A | N/A | D | 同上。 |
| row 間の空き | 8 | 4.6 | N/A | N/A | D | 同上。 |
| Header 終端→最初の row | 11 | 6.3 | N/A | N/A | D | 同上。 |
| Content 左端→ListItem 左端 | 25 | 14.3 | N/A | N/A | C | ウィンドウ・Navigation Pane・列状態の影響を受けるため、複数 DPI を得ても固定基準値にはしない。 |
| ListItem 左端→名前 Text 左端 | 39 | 22.3 | N/A | N/A | D | 別 DPI の UIA Text 矩形を取得できない。 |
| Shell small icon サイズ | 28 × 28 | 16.0 × 16.0 | N/A | N/A | B | 168 DPI では `SM_CXSMICON/SM_CYSMICON` と一致。別 DPI API 実測がないため、16 logical px は候補に留める。 |
| 選択 row 高さ | 43 | 24.6 | N/A | N/A | D | 別 DPI の選択行 UIA 矩形を取得できない。 |
| 選択領域 左 inset | 25 | 14.3 | N/A | N/A | C | Content 左端の状態依存値であり、固定化しない。 |
| vertical scrollbar 操作領域幅 | 30 | 17.1 | N/A | N/A | D | 別 DPI の UIA/API 値を取得できない。 |
| Status area 高さ | 48 | 27.4 | N/A | N/A | D | 別 DPI の同一状態スクリーンショットを取得できない。 |
| separator | 1 physical px | 0.57 | N/A | N/A | D | 別 DPI での物理幅が未測定。 |

### 基準値の判定

今回の別 DPI 実測は成立していないため、**A（複数 DPI で安定し、採用可能）に新たに分類できる値はない**。前回の 168 DPI のみから、以下を「実装へ確定転記」とはしない。

| 前回の候補 | 今回の結論 | 判定 | 理由 |
| --- | --- | --- | --- |
| Navigation Button = 32 logical px | 継続候補 | D | 168 DPI では 56 physical px → 32.0 logical px。ただし別 DPI 比較ができない。 |
| Address Bar height = 32 logical px | 継続候補 | D | 168 DPI では 56 physical px → 32.0 logical px。ただし別 DPI 比較ができない。 |
| Command icon = 16 logical px | 継続候補 | D | UIA Image 外接矩形の値であり、Glyph 実線寸法ではない。別 DPI 未測定。 |
| Shell small icon = 16 logical px | 要注意の継続候補 | B | 168 DPI の `SM_CXSMICON = 28` は 16.0 logical px。別 DPI API 値による確認が必要。 |
| icon-only Command Button = 40 logical px | 継続候補 | D | 168 DPI では 70 physical px → 40.0 logical px。ただし別 DPI 未測定。 |
| Command Button gap = 約 8 logical px | 継続候補 | D | 168 DPI では 14 physical px → 8.0 logical px。ただし別 DPI 未測定。 |
| Command Bar = 44.6 logical px | 固定値にしない | D | 丸め後の整数値へ寄せる根拠がない。 |
| Details Header = 27.4 logical px | 固定値にしない | D | 同上。 |
| Details row = 24.6 logical px | 固定値にしない | D | 同上。 |
| Status area = 27.4 logical px | 固定値にしない | D | 同上。 |

### Hairline の再判定

前回は 168 DPI で Address/Search border と各 separator が **1 physical px** として観測された。この再測定では別 DPI の同じ境界を測れなかったため、次のどちらかを確定できない。

- DPI に比例する `1 logical px × dpiScale` の線である。
- DPI にかかわらず 1 physical px で描かれる hairline である。

したがって separator / border の実装規則は **D（判定不能）** とする。現時点で `1 logical px` を DPI 倍して採用してはならない。少なくとも 96 DPI または 144 DPI の実 Explorer で、同一箇所の物理幅を再測定してから決定する。

### 次回の再測定条件

- 実ユーザーセッション上で 96 DPI または 144 DPI の Explorer ウィンドウを UI Automation から取得する。
- `GetDpiForWindow(hwnd)` と `BoundingRectangle` を同じ対象ウィンドウで記録する。
- Status/separator は同じウィンドウ状態・同じ表示倍率のスクリーンショットで測る。
- 物理 px を `physical px × 96 / DPI` で換算し、小数第 1 位を保持してこの表の N/A を実測値に置き換える。

## 測定条件と記号

- 測定日: 2026-08-25
- 対象: このPCで実行中の Windows 11 File Explorer（Details View、ドキュメント）
- 実測DPI: 168 DPI（175%）。`GetDpiForWindow` で取得。
- 論理px換算: `physical px × 96 / 168`、すなわち `physical px / 1.75`。
- `M-UIA`: UI Automation の `BoundingRectangle` から得た実測値。
- `M-SS`: 同一168 DPIの実Explorer画面から読んだ描画上の実測値。ピクセル境界は ±1 physical px。
- `A-API`: Windows API から得た実測値。UIそのものの矩形ではない。
- `N/A`: 公開UI Automationと画面上の静的測定では取得不能。推測値を置かない。

注記: UI Automationは矩形・ControlType・有効状態を返すが、フォントサイズ、字間、角丸半径、罫線の太さ、hover色は返さない。これらをUIA値と誤認しない。

## 上部: Back / Forward / Up / Refresh 行

| 項目 | physical px | 96 DPI logical px | 種別 | 備考 |
| --- | ---: | ---: | --- | --- |
| 行の外接高さ | 85 | 48.6 | M-UIA | 上部ナビゲーション／アドレス行全体。 |
| Back hitbox | 56 × 56 | 32 × 32 | M-UIA | `戻る` Button。 |
| Forward hitbox | 56 × 56 | 32 × 32 | M-UIA | `進む` Button。無効状態でも寸法は同じ。 |
| Up hitbox | 56 × 56 | 32 × 32 | M-UIA | `Alt+上矢印` Button。 |
| Refresh hitbox | 56 × 56 | 32 × 32 | M-UIA | `F5` Button。 |
| 隣接ナビゲーションButtonのgap | 28 | 16.0 | M-UIA | Button外接矩形間。 |
| 行左端からBack hitboxまで | 19 | 10.9 | M-UIA | 対象ウィンドウのクライアント上部Paneを基準。 |
| 行上端→Button上端 | 13 | 7.4 | M-UIA | 下側は16 physical px / 9.1 logical px。 |
| Refresh→Address Bar | 25 | 14.3 | M-UIA | hitbox終端からAddress Bar外接矩形まで。 |
| Glyph描画サイズ | N/A | N/A | N/A | Button内部のGlyphはControl Viewに要素として公開されない。 |
| Glyphと文字のgap | N/A | N/A | N/A | この4ボタンは対象状態ではアイコンのみ。 |
| hover範囲 | 56 × 56 | 32 × 32 | M-UIA | Buttonの操作範囲。描画上のhover角丸は別測定不可。 |

## Breadcrumb / Address Bar / Search Box

| 項目 | physical px | 96 DPI logical px | 種別 | 備考 |
| --- | ---: | ---: | --- | --- |
| Address Bar外接高さ | 56 | 32.0 | M-UIA | Address Bar Edit。 |
| Address Bar外接幅 | 743 | 424.6 | M-UIA | フォルダ名・ウィンドウ幅により可変。固定幅ではない。 |
| Search Box外接高さ | 56 | 32.0 | M-UIA | Search Edit。 |
| Search Box外接幅 | 290 | 165.7 | M-UIA | フォルダ名・ウィンドウ幅により可変。 |
| Address Bar→Search Box gap | 14 | 8.0 | M-UIA | 外接矩形間。 |
| Breadcrumb個別要素のhitbox | N/A | N/A | N/A | Control ViewではAddress Bar内の個別Breadcrumbが公開されなかった。 |
| Breadcrumb icon / text gap | N/A | N/A | N/A | 同上。 |
| Address/Search角丸半径 | N/A | N/A | N/A | UIA非公開。画面比較時にのみ別測定する。 |
| Address/Search border | 1 physical px | 0.57 at 168 DPI | M-SS | 物理1pxのhairlineとして描画されている。論理1pxへ機械換算して太くしない。 |
| Address/Search本文フォントサイズ | N/A | N/A | N/A | UIAはフォントポイント数を公開しない。 |

## Command Bar

| 項目 | physical px | 96 DPI logical px | 種別 | 備考 |
| --- | ---: | ---: | --- | --- |
| Command Bar外接高さ | 78 | 44.6 | M-UIA | New / Cut / Copy / Paste / Rename / Share / Delete / Sort / View / More 行。 |
| New Button外接幅 | 191 | 109.1 | M-UIA | 文字を含む可変幅Button。 |
| icon-only Button外接幅 | 70 | 40.0 | M-UIA | Cut等。高さはCommand Barと同じ78 physical px。 |
| icon-only Button間gap | 14 | 8.0 | M-UIA | Cut→Copy等。 |
| Button左余白（Button→Image） | 20 | 11.4 | M-UIA | New/Cutの実測。 |
| Command icon外接サイズ | 28 × 28 | 16 × 16 | M-UIA | Image要素。Glyphの実線範囲ではない。 |
| Command Bar上端→icon上端 | 28 | 16.0 | M-UIA | iconを中央配置した結果。 |
| Command text font size | N/A | N/A | N/A | UIA非公開。 |
| icon→label gap | N/A | N/A | N/A | Button内部Textの個別矩形がControl Viewに公開されなかった。 |
| hover操作範囲 | Button外接矩形 | Button外接矩形 | M-UIA | 視覚的なhover塗りの角丸はN/A。 |
| 無効状態の操作範囲 | Button外接矩形 | Button外接矩形 | M-UIA | Cut等は無効でも同じ寸法。 |

## Details View header / row

| 項目 | physical px | 96 DPI logical px | 種別 | 備考 |
| --- | ---: | ---: | --- | --- |
| Command Bar終端→Header開始 | 4 | 2.3 | M-UIA | separatorを含む境界間隔。 |
| Header外接高さ | 48 | 27.4 | M-UIA | `名前` / `更新日時` / `種類` / `サイズ` Header。 |
| Headerの最初の列幅 | 536 | 306.3 | M-UIA | ウィンドウ幅・列編集で可変。固定値ではない。 |
| Details row外接高さ | 43 | 24.6 | M-UIA | ListItemの実測。 |
| row pitch | 51 | 29.1 | M-UIA | 連続するListItem上端の差。 |
| row間の空き | 8 | 4.6 | M-UIA | `pitch - row height`。 |
| Header終端→最初のrow | 11 | 6.3 | M-UIA | Header下端から先頭ListItem上端。 |
| row上端→Text Edit上端 | 5 | 2.9 | M-UIA | 下側は4 physical px / 2.3 logical px。 |
| Text Edit行高 | 34 | 19.4 | M-UIA | フォントサイズではなく、公開されたテキスト領域の高さ。 |
| Content左端→ListItem左端 | 25 | 14.3 | M-UIA | row selectionの左側inset。 |
| ListItem左端→名前Text左端 | 39 | 22.3 | M-UIA | アイコン領域と名前前余白を含む。 |
| file/folder iconシステムサイズ | 28 × 28 | 16 × 16 | A-API | `SM_CXSMICON` / `SM_CYSMICON` を168 DPIで取得。実Explorerの小アイコン表示と整合。 |
| file/folder icon→名前の個別gap | N/A | N/A | N/A | アイコン子要素はControl Viewに公開されない。上記39px内訳を推測しない。 |
| Header/rowフォントサイズ | N/A | N/A | N/A | UIA非公開。Text Edit行高だけを記録。 |

## Selection / hover

| 項目 | physical px | 96 DPI logical px | 種別 | 備考 |
| --- | ---: | ---: | --- | --- |
| 選択row高さ | 43 | 24.6 | M-UIA / M-SS | ListItemと同じ高さ。 |
| 選択領域の左inset | 25 | 14.3 | M-UIA | Content Paneから選択矩形まで。 |
| 選択領域の右端 | vertical scrollbar左端 | vertical scrollbar左端 | M-SS | 横スクロール時の仮想ListItem幅ではなく、見えている選択塗りの終端。 |
| 選択領域の可視幅 | 1,059 | 605.1 | M-SS | 今回のウィンドウ幅・列状態での値。可変。 |
| hover操作範囲 | ListItem外接矩形 | ListItem外接矩形 | M-UIA | UIAのhit target。hover塗りの実画素範囲はポインター移動試験をしていないため未記録。 |
| hover角丸半径 | N/A | N/A | N/A | 静的画面とUIAからは取得不能。 |

## Scrollbar / Status area / separator

| 項目 | physical px | 96 DPI logical px | 種別 | 備考 |
| --- | ---: | ---: | --- | --- |
| vertical scrollbar操作領域幅 | 30 | 17.1 | M-UIA / A-API | UIAの垂直Paneと`SM_CXVSCROLL`が一致。 |
| scrollbar thumbの描画幅 | N/A | N/A | N/A | 表示内容・状態で変化し、静的UIAでは公開されない。 |
| Status area高さ | 48 | 27.4 | M-SS | 画面下の項目数・選択数を表示する領域。 |
| Status text font size | N/A | N/A | N/A | UIA非公開。 |
| 領域separator幅 | 1 physical px | 0.57 at 168 DPI | M-SS | Address/Command/Content/Status境界のhairline。 |
| separatorの角丸 | なし | なし | M-SS | 直線。 |

## 実装に転記してよい／転記してはいけない値

- 転記候補: 32 logical px のナビゲーションhitbox、16 logical pxのCommand/Shell小アイコン、32 logical pxのAddress/Search高さ、40 logical pxのicon-only Command Button幅、8 logical px前後のCommand gap。
- 要再測定: Command Bar 44.6、Header 27.4、row 24.6、Status 27.4。実Explorerのバージョン、DPI、ウィンドウ幅、OneDrive状態で変動するため、整数へ丸めて自動採用しない。
- 転記禁止: `N/A` の欄を推測値で埋めること。この記録は実装へ自動接続しない。
