# 1_44MB

raylib を使った 2D 横スクロールゲームの土台です。

ゲーム本体の主人公は `assets/Sprite/ZIPPER.png` の 5 フレームアニメーションを使用します。移動中にフレームが切り替わります。画像は `assets` に置いたまま参照し、`build` にはコピーしません。

## フォルダ構成

- `src/Play`: ゲーム本体
- `src/Editor`: エディター
- `src/Shered`: 両方から利用するプレイヤー・ステージ
- `assets/Sprite`: スプライト画像
- `build`: 実行ファイルと必要な DLL

## 操作

- `A` / `←`: 左へ移動
- `D` / `→`: 右へ移動
- `Esc`: 終了

## エディター

`build/side_scroller_editor.exe` を起動すると、主人公の見た目を確認・変更できます。

- 主人公を左クリックするとインスペクターを表示します。
- インスペクターの「PNG を選択」から PNG ファイルを指定すると、その場で見た目に反映されます。

## ビルド（MSYS2 MinGW64）

MSYS2 MinGW64 シェルで、プロジェクト直下から次を実行します。

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Isrc/Shered \
  src/Play/main.c src/Shered/player.c src/Shered/stage.c \
  -o build/side_scroller.exe -lraylib -lopengl32 -lgdi32 -lwinmm

gcc -std=c11 -Wall -Wextra -Wpedantic -Isrc/Editor -Isrc/Shered \
  src/Editor/main.c src/Editor/editor_ui.c src/Editor/file_dialog.c \
  src/Shered/player.c src/Shered/stage.c -o build/side_scroller_editor.exe \
  -lraylib -lcomdlg32 -lopengl32 -lgdi32 -lwinmm
```

実行ファイルと必要な DLL は `build` にまとめます。ゲーム本体を起動するには、`build` と同じ階層に `assets` フォルダが必要です。
