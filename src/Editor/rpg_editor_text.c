// 役割: RPG エディターの UTF-8 テキスト計測とカーソル描画を実装する。
// 依存する自プロジェクト内ファイル: game_font.h, rpg_editor_text.h
#include "rpg_editor_text.h"

#include "game_font.h"
#include "rpg_dialogue.h"

#include <string.h>

static bool isKeyboardCaptured;

void RpgEditorText_SetKeyboardCapture(bool captured)
{
    isKeyboardCaptured = captured;
}

void RpgEditorText_ClaimKeyboard(void)
{
    isKeyboardCaptured = true;
}

bool RpgEditorText_IsKeyboardCaptured(void)
{
    return isKeyboardCaptured;
}

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

void RpgEditorText_BeginPointerSelection(const char *text, float localX, float fontSize,
                                         int *cursorIndex, int *selectionAnchor, int *selectionEnd)
{
    if (text == NULL || cursorIndex == NULL || selectionAnchor == NULL || selectionEnd == NULL) return;
    int index = RpgEditorText_GetCursorIndexAtX(text, localX, fontSize);
    *cursorIndex = index;
    *selectionAnchor = index;
    *selectionEnd = index;
}

void RpgEditorText_UpdatePointerSelection(const char *text, float localX, float fontSize,
                                          int *cursorIndex, int *selectionEnd)
{
    if (text == NULL || cursorIndex == NULL || selectionEnd == NULL) return;
    int index = RpgEditorText_GetCursorIndexAtX(text, localX, fontSize);
    *cursorIndex = index;
    *selectionEnd = index;
}

bool RpgEditorText_ShouldEndEditingOnOutsideClick(Rectangle fieldBounds, Vector2 pointer)
{
    return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(pointer, fieldBounds);
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

static int GetPreviousUtf8Index(const char *text, int index)
{
    if (index <= 0) return 0;
    index--;
    while (index > 0 && ((unsigned char)text[index] & 0xc0) == 0x80) index--;
    return index;
}

static bool DeleteSelection(char *text, int *cursorIndex, int *selectionAnchor, int *selectionEnd)
{
    int start = *selectionAnchor < *selectionEnd ? *selectionAnchor : *selectionEnd;
    int end = *selectionAnchor < *selectionEnd ? *selectionEnd : *selectionAnchor;
    if (start == end) return false;
    memmove(text + start, text + end, strlen(text + end) + 1);
    *cursorIndex = start;
    *selectionAnchor = start;
    *selectionEnd = start;
    return true;
}

static int EncodeUtf8(int codepoint, char encoded[5])
{
    if (codepoint <= 0x7f) {
        encoded[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7ff) {
        encoded[0] = (char)(0xc0 | (codepoint >> 6));
        encoded[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2;
    }
    if (codepoint <= 0xffff) {
        encoded[0] = (char)(0xe0 | (codepoint >> 12));
        encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3;
    }
    encoded[0] = (char)(0xf0 | (codepoint >> 18));
    encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    encoded[3] = (char)(0x80 | (codepoint & 0x3f));
    return 4;
}

static void InsertCodepoint(char *text, size_t capacity, int *cursorIndex, int codepoint)
{
    char encoded[5] = { 0 };
    int encodedLength = EncodeUtf8(codepoint, encoded);
    size_t length = strlen(text);
    if ((size_t)*cursorIndex > length || length + (size_t)encodedLength >= capacity) return;
    memmove(text + *cursorIndex + encodedLength, text + *cursorIndex,
            length - (size_t)*cursorIndex + 1);
    memcpy(text + *cursorIndex, encoded, (size_t)encodedLength);
    *cursorIndex += encodedLength;
}

static void RemoveBeforeCursor(char *text, int *cursorIndex)
{
    if (*cursorIndex <= 0) return;
    int previous = GetPreviousUtf8Index(text, *cursorIndex);
    memmove(text + previous, text + *cursorIndex, strlen(text + *cursorIndex) + 1);
    *cursorIndex = previous;
}

void RpgEditorText_UpdateInput(char *text, size_t capacity, int *cursorIndex,
                               int *selectionAnchor, int *selectionEnd)
{
    if (text == NULL || capacity == 0 || cursorIndex == NULL ||
        selectionAnchor == NULL || selectionEnd == NULL) return;
    RpgEditorText_ClaimKeyboard();
    int textLength = (int)strlen(text);
    if (*cursorIndex < 0) *cursorIndex = 0;
    if (*cursorIndex > textLength) *cursorIndex = textLength;
    bool hasControl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool isSelectAll = hasControl && IsKeyPressed(KEY_A);
    if (isSelectAll) {
        *cursorIndex = textLength;
        *selectionAnchor = 0;
        *selectionEnd = textLength;
    }
    if (IsKeyPressed(KEY_LEFT)) *cursorIndex = GetPreviousUtf8Index(text, *cursorIndex);
    if (IsKeyPressed(KEY_RIGHT)) *cursorIndex = GetNextUtf8Index(text, *cursorIndex);
    if (IsKeyPressed(KEY_HOME)) *cursorIndex = 0;
    if (IsKeyPressed(KEY_END)) *cursorIndex = textLength;
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_HOME) || IsKeyPressed(KEY_END)) {
        *selectionAnchor = *cursorIndex;
        *selectionEnd = *cursorIndex;
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !DeleteSelection(text, cursorIndex, selectionAnchor, selectionEnd))
        RemoveBeforeCursor(text, cursorIndex);
    if (IsKeyPressed(KEY_DELETE) && !DeleteSelection(text, cursorIndex, selectionAnchor, selectionEnd) &&
        text[*cursorIndex] != '\0') {
        int next = GetNextUtf8Index(text, *cursorIndex);
        memmove(text + *cursorIndex, text + next, strlen(text + next) + 1);
    }
    bool isPaste = hasControl && IsKeyPressed(KEY_V);
    if (isPaste) {
        const char *clipboard = GetClipboardText();
        DeleteSelection(text, cursorIndex, selectionAnchor, selectionEnd);
        while (clipboard != NULL && *clipboard != '\0') {
            int byteCount = 0;
            int codepoint = GetCodepointNext(clipboard, &byteCount);
            if (byteCount <= 0) break;
            if (codepoint >= 32) InsertCodepoint(text, capacity, cursorIndex, codepoint);
            clipboard += byteCount;
        }
        *selectionAnchor = *cursorIndex;
        *selectionEnd = *cursorIndex;
        GameFont_AddText(text);
    }
    for (int codepoint = GetCharPressed(); codepoint > 0; codepoint = GetCharPressed()) {
        if (!isPaste && !isSelectAll && codepoint >= 32) {
            DeleteSelection(text, cursorIndex, selectionAnchor, selectionEnd);
            InsertCodepoint(text, capacity, cursorIndex, codepoint);
            GameFont_AddText(text);
        }
    }
}
