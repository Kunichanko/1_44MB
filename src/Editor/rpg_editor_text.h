// 役割: RPG エディターで共用する UTF-8 テキストのカーソル位置と描画補助 API を宣言する。
#ifndef RPG_EDITOR_TEXT_H
#define RPG_EDITOR_TEXT_H

#include "raylib.h"
#include <stddef.h>

int RpgEditorText_GetCursorIndexAtX(const char *text, float x, float fontSize);
int RpgEditorText_GetWrappedLineCount(const char *text, float fontSize, float width);
void RpgEditorText_DrawCaret(int x, int y, int height);

/* Pointer selection uses UTF-8 byte indices shared by every editor field.
 * Call Begin on a field press and Update while the same press is held. */
void RpgEditorText_BeginPointerSelection(const char *text, float localX, float fontSize,
                                         int *cursorIndex, int *selectionAnchor, int *selectionEnd);
void RpgEditorText_UpdatePointerSelection(const char *text, float localX, float fontSize,
                                          int *cursorIndex, int *selectionEnd);
/* A focused field uses this shared blur rule: a left press outside its own
 * bounds ends text editing without changing the click target's behavior. */
bool RpgEditorText_ShouldEndEditingOnOutsideClick(Rectangle fieldBounds, Vector2 pointer);

/* Shared focus gate for every editor text field.  Call SetKeyboardCapture at
 * the start of a frame from the active-field state; every text update also
 * claims it defensively.  Non-text shortcuts must consult this instead of
 * duplicating per-inspector focus checks. */
void RpgEditorText_SetKeyboardCapture(bool captured);
void RpgEditorText_ClaimKeyboard(void);
bool RpgEditorText_IsKeyboardCaptured(void);
void RpgEditorText_UpdateInput(char *text, size_t capacity, int *cursorIndex,
                               int *selectionAnchor, int *selectionEnd);

#endif
