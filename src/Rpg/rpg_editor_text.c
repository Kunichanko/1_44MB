// 役割: RPG エディターの UTF-8 テキスト計測とカーソル描画を実装する。
// 依存する自プロジェクト内ファイル: game_font.h, rpg_editor_text.h
#include "rpg_editor_text.h"

#include "game_font.h"
#include "rpg_dialogue.h"

#include <string.h>

static int GetNextUtf8Index(const char *text, int index)
{
    int byteCount = 0;
    if (text[index] == '\0') return index;
    GetCodepointNext(text + index, &byteCount);
    return index + byteCount;
}

int RpgEditorText_GetCursorIndexAtX(const char *text, float x, float fontSize)
{
    int index = 0;
    char prefix[RPG_DIALOGUE_LINE_LENGTH];
    while (text[index] != '\0') {
        int nextIndex = GetNextUtf8Index(text, index);
        memcpy(prefix, text, (size_t)nextIndex);
        prefix[nextIndex] = '\0';
        float rightX = GameFont_MeasureText(prefix, fontSize).x;
        if (x < rightX) {
            memcpy(prefix, text, (size_t)index);
            prefix[index] = '\0';
            float leftX = GameFont_MeasureText(prefix, fontSize).x;
            return x - leftX < rightX - x ? index : nextIndex;
        }
        index = nextIndex;
    }
    return index;
}

int RpgEditorText_GetWrappedLineCount(const char *text, float fontSize, float width)
{
    int lineCount = 1;
    int lineStart = 0;
    int index = 0;
    char part[RPG_DIALOGUE_LINE_LENGTH];
    while (text[index] != '\0') {
        int nextIndex = GetNextUtf8Index(text, index);
        memcpy(part, text + lineStart, (size_t)(nextIndex - lineStart));
        part[nextIndex - lineStart] = '\0';
        if (index > lineStart && GameFont_MeasureText(part, fontSize).x > width) {
            lineCount++;
            lineStart = index;
        } else index = nextIndex;
    }
    return lineCount;
}

void RpgEditorText_DrawCaret(int x, int y, int height)
{
    if ((int)(GetTime() * 2.0) % 2 == 0) DrawRectangle(x - 1, y, 3, height, PURPLE);
}
