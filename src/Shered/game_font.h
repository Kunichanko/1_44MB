// 依存: なし（raylib は外部ライブラリ）
#ifndef GAME_FONT_H
#define GAME_FONT_H

#include "raylib.h"

#include <stdbool.h>

bool GameFont_Load(const char *filePath);
void GameFont_Unload(void);
void GameFont_Draw(const char *text, float x, float y, float fontSize, Color color);

#endif
