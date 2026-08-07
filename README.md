# 1_44MB

raylib を使った 2D 横スクロールゲームの土台です。

ゲーム本体の主人公は `assets/Sprite/ZIPPER.png` の 5 フレームアニメーションを使用します。移動中にフレームが切り替わります。画像は `assets` に置いたまま参照し、`build` にはコピーしません。

敵キャラは3体おり、`assets/Sprite/FILE.png` を使用して指定された巡回範囲を左右に往復します。従属化後の補間移動だけはプレイヤーの移動速度を上限にし、最初の敵は「設定した間隔 × プレイヤー倍率」の位置を目標に、2体目以降はひとつ前の敵との設定間隔を取るよう追従します。k体目の目標は、プレイヤーの `k×0.05秒` 前の位置から計算し、毎フレーム更新します。

## フォルダ構成

- `src/Play`: ゲーム本体
- `src/Editor`: エディター
- `src/Shered`: 両方から利用するプレイヤー・ステージ
- `assets/Sprite`: スプライト画像
- `assets/Settings`: エディターで保存するゲーム設定
- `assets/Fonts`: 日本語表示用フォント（`NotoSansJP-VF.ttf`）
- `build`: 実行ファイルと必要な DLL

## 操作

- `A` / `←`: 左へ移動
- `D` / `→`: 右へ移動
- `Space`: 主人公のアクションを一度再生。アニメーション中は移動できず、220px以内の敵を従属化する。紫の円が判定範囲
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
