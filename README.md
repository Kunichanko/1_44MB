# 1_44MB

raylib を使った 2D 横スクロールゲームの土台です。

## 操作

- `A` / `←`: 左へ移動
- `D` / `→`: 右へ移動
- `Esc`: 終了

## エディター

`bin/side_scroller_editor.exe` を起動すると、主人公の見た目を確認・変更できます。

- 主人公を左クリックするとインスペクターを表示します。
- インスペクターの「PNG を選択」から PNG ファイルを指定すると、その場で見た目に反映されます。

## ビルド（MSYS2 MinGW64）

MSYS2 MinGW64 シェルで、プロジェクト直下から次を実行します。

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic src/main.c src/player.c src/stage.c \
  -o bin/side_scroller.exe -lraylib -lopengl32 -lgdi32 -lwinmm

gcc -std=c11 -Wall -Wextra -Wpedantic src/editor_main.c src/editor_ui.c \
  src/file_dialog.c src/player.c src/stage.c -o bin/side_scroller_editor.exe \
  -lraylib -lcomdlg32 -lopengl32 -lgdi32 -lwinmm
```

実行ファイルは `bin/side_scroller.exe` に出力されます。
