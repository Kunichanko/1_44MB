// 依存: なし（raylib は外部ライブラリ）
#ifndef GAME_FONT_H
#define GAME_FONT_H

#include "raylib.h"
#include "rpg_text.h"

#include <stdbool.h>

bool GameFont_Load(const char *filePath);
/* Collect a group of strings before rebuilding the atlas once. Dynamic text
 * outside a batch keeps its immediate update behaviour. */
void GameFont_BeginTextBatch(void);
bool GameFont_EndTextBatch(void);
bool GameFont_AddText(const char *text);
void GameFont_Unload(void);
RpgTextRenderer GameFont_GetTextRenderer(void);
RpgTextPresetSet GameFont_GetTextPresetSet(void);
void GameFont_DrawPreset(RpgTextPreset preset, const char *text, float x, float y, float sizeScale);
Vector2 GameFont_MeasurePreset(RpgTextPreset preset, const char *text, float sizeScale);
float GameFont_GetPresetScale(RpgTextPreset preset, float requestedSize);
void GameFont_Draw(const char *text, float x, float y, float fontSize, Color color);
Vector2 GameFont_MeasureText(const char *text, float fontSize);

/*
 * raylib の通常テキストAPIと同じ引数で、共有Font atlasへ描画を集約する。
 * 呼び出し元は logical px のまま渡せる。これにより描画と幅計測が常に同じ
 * フォント・fallback 経路を使う。
 */
void GameFont_DrawText(const char *text, int x, int y, int fontSize, Color color);
int GameFont_MeasureTextPixels(const char *text, int fontSize);

/*
 * raylib の DrawText / MeasureText を使っていた既存のゲーム・エディター
 * 描画コード向けの移行スイッチ。Win32 API名との衝突を避けるため、各実装
 * ファイルが raylib / Windows header を読み終えた後にだけ有効化する。
 */
#ifdef RPG_TEXT_ROUTE_RAYLIB_CALLS
#define DrawText(text, x, y, fontSize, color) GameFont_DrawText((text), (x), (y), (fontSize), (color))
#define MeasureText(text, fontSize) GameFont_MeasureTextPixels((text), (fontSize))
#endif

#endif
// 役割: 共通フォントの登録・描画 API を宣言する。
