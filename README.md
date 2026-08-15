# 1_44MB

raylibで作成した2D横スクロールゲームと、そのRPG版エディターです。

## 実行ファイル

- `build/rpg_version.exe`: RPGゲーム
- `build/rpg_editor.exe`: RPGステージ・キャラクター・会話の編集

## RPGエディター

- `1` / `2` / `3`: 編集するマップを切替
- `B`: ブロック編集モード
- `S`: 編集内容を保存
- `Ctrl` + `Z`: ブロック編集時は直前のマス変更を戻す。インスペクター表示中は、編集値・ステージ移動・対象選択・モーダル遷移を戻す。履歴は50件までで、エディターを閉じた後も維持
- `Revert saved`: 未保存の編集内容を直近の保存状態へ戻す
- プレイヤー、NPC、Zipperを選択: 位置・大きさなどを編集
- 各キャラクターのインスペクター: `Save`でそのキャラクターの位置・大きさだけを保存。隣の`Revert`でその保存状態へ戻す
- Zipper設定: 位置・大きさ・調べるFunctionをZipper専用設定として管理
- Zipperを調べる: 設定したFunction列が完了すると主人公へ追従
- 未保存の変更がある状態で閉じる: 保存して終了／保存せず終了／キャンセルを選択。`Show details`で未保存項目を確認し、一覧はマウスホイールでスクロール

NPCの `Edit dialogue` では通常会話を、`Edit examine` では調べる機能を編集できます。Examineではタイトル付きの会話機能を管理し、各機能ごとに会話を編集します。

- `Edit dialogue`: NPCに話しかけた時の会話を編集
- `Edit examine`: NPCを調べた時の機能一覧を開く
- Examine一覧: 1クリックで会話機能を選択してタイトルを編集、ダブルクリックで会話内容を編集。`Add function` で機能タイプを選択して追加、右クリックで削除、会話ブロックをドラッグして順番変更
- Move機能: 実際のゲームでは選択対象の本体を移動させる。エディターのプレビューでは同じ見た目のダミースプライトだけを移動させ、実際の配置は変更しない。Moveサイドパネルの外側を左クリックして目的地、右クリックしてプレビュー専用の開始位置を設定。時間を調整し、`Play preview` で動きを確認。各`Save`の隣の`Revert`はその編集内容だけを戻し、全体保存は`S`で行う。サイドパネルの空白部分はドラッグで移動可能
- モーダル画面: `Tab` で一つ前の画面へ戻る。Examineの会話編集は一覧へ、一覧ではエディター画面へ戻る
- Zipper: インスペクターの `Edit examine` から調べる機能を編集

## RPGゲーム

- 移動: `A` / `D` または矢印キー
- ジャンプ: `W` / `Space`
- NPC会話: `E`
- NPC・Zipperを調べる: `I` で開始、`E` で進行。一度完了すると、そのゲーム中は再実行しない
- カメラ切替: `C`

ゲームは `assets/Settings` の保存内容を読み込みます。画像やフォントなどの外部ファイルは `assets` に置き、`build` には追加しません。
- `build`: 実行ファイルと必要な DLL
- `experiment`: 実験用スペース（各自ローカル。`.gitignore` 済み。詳細は `experiment/README.md` と `experiment/RULES.md`）

## ステージグリッド

ステージは96px四方のグリッドで管理します。標準サイズのプレイヤーと敵は1マス内に収まります。

- `0`: 空白
- `1`: 地面・壁ブロック
- `2`: 敵がいるマス（敵の移動に合わせて更新）

## 操作

- `A` / `←`: 左へ移動
- `D` / `→`: 右へ移動
- `Space`: 主人公のアクションを一度再生。アニメーション中は移動できず、220px以内の敵を従属化する。紫の円が判定範囲
- `Q`: グリッド確認モードを切替。画面を淡くし、各マスの `0` / `1` / `2` を表示
- `Esc`: 終了

## エディター

`build/side_scroller_editor.exe` を起動すると、主人公の見た目を確認・変更できます。

- 主人公を左クリックするとインスペクターを表示します。
- インスペクターの「PNG を選択」から PNG ファイルを指定すると、その場で見た目に反映されます。
- プレイヤーと敵は標準で1.5倍の大きさで表示されます。各インスペクターの `Scale` で0.5〜3.0倍に変更できます。
- 敵を左クリックすると敵インスペクターを表示し、追従間隔・補間速度・従属時の色を変更できます。
- インスペクターを開いたクリックはボタン操作として扱わず、インスペクター外を左クリックすると閉じます。
- 追従間隔と補間速度はスライダーのほか、右側の数値欄をクリックしてキーボード入力でも変更できます。最初のクリック後に数値を入力すると値全体を上書きし、同じ欄をもう一度クリックすると、その桁の位置から編集できます。Enterで確定します。保存結果は敵インスペクター下部に表示されます。
- プレイヤーと敵の `Scale` 入力欄は独立しており、片方を編集した値がもう片方へ引き継がれることはありません。
- 「設定を保存」で `assets/Settings/enemy_settings.cfg` に保存され、保存成功時は敵インスペクターが閉じます。ゲーム本体の次回起動時に反映されます。
- Playボタンでエディター内の動作確認を開始できます。停止中は敵は動かず、Play中はゲーム本体と同様に `A` / `D` と `Space` を操作できます。
- エディターでも `Q` でグリッド確認モードを切り替えられます。
- Playボタン横の `Global` を押すと全体インスペクターを開けます。`Grid opacity` で、グリッド確認モード時に画面を淡くする度合いを変更・保存できます。

## ビルド（MSYS2 MinGW64）

MSYS2 MinGW64 シェルで、プロジェクト直下から次を実行します。

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Isrc/Shered \
  src/Play/main.c src/Shered/player.c src/Shered/stage.c \
  src/Shered/enemy.c src/Shered/enemy_group.c src/Shered/enemy_settings.c src/Shered/game_font.c \
  -o build/side_scroller.exe -lraylib -lopengl32 -lgdi32 -lwinmm

gcc -std=c11 -Wall -Wextra -Wpedantic -Isrc/Editor -Isrc/Shered \
  src/Editor/main.c src/Editor/editor_ui.c src/Editor/file_dialog.c \
  src/Shered/enemy.c src/Shered/enemy_group.c src/Shered/enemy_settings.c \
  src/Shered/game_font.c src/Shered/player.c src/Shered/stage.c \
  -o build/side_scroller_editor.exe \
  -lraylib -lcomdlg32 -lopengl32 -lgdi32 -lwinmm
```

実行ファイルと必要な DLL は `build` にまとめます。ゲーム本体を起動するには、`build` と同じ階層に `assets` フォルダが必要です。
>>>>>>> d564e0fc4bfb9115db6ec48e7671889cd091b5ef
