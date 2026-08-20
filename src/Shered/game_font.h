// 依存: なし（raylib は外部ライブラリ）
#ifndef GAME_FONT_H
#define GAME_FONT_H

#include "raylib.h"

#include <stdbool.h>

bool GameFont_Load(const char *filePath);
bool GameFont_AddText(const char *text);
void GameFont_Unload(void);
void GameFont_Draw(const char *text, float x, float y, float fontSize, Color color);
Vector2 GameFont_MeasureText(const char *text, float fontSize);

#endif
// 役割: 共通フォントの登録・描画 API を宣言する。
