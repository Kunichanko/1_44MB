// 役割: RPG エディターで共用する UTF-8 テキストのカーソル位置と描画補助 API を宣言する。
#ifndef RPG_EDITOR_TEXT_H
#define RPG_EDITOR_TEXT_H

#include "raylib.h"

int RpgEditorText_GetCursorIndexAtX(const char *text, float x, float fontSize);
int RpgEditorText_GetWrappedLineCount(const char *text, float fontSize, float width);
void RpgEditorText_DrawCaret(int x, int y, int height);

#endif
