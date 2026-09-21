#ifndef RPG_TEXT_H
#define RPG_TEXT_H

#include "raylib.h"

/*
 * UIごとのフォント選択はbackendへ閉じ込め、呼び出し側はこの共通入口だけを
 * 使う。ゲーム／エディターはGameFont、ExplorerはWindows UI font backendを
 * 登録するため、見た目の要件を保ったまま文字描画の責務を統一できる。
 */
typedef void (*RpgTextDrawBackend)(const void *context, const char *text,
                                   Vector2 position, float size, Color color);
typedef Vector2 (*RpgTextMeasureBackend)(const void *context, const char *text, float size);

typedef struct RpgTextRenderer {
    const void *context;
    RpgTextDrawBackend draw;
    RpgTextMeasureBackend measure;
} RpgTextRenderer;

/* All visible text belongs to one of these semantic presentation roles.
 * A renderer may supply a different font backend for each role, while callers
 * only choose a role and a layout scale. */
typedef enum RpgTextPreset {
    RPG_TEXT_PRESET_GAME = 0,
    RPG_TEXT_PRESET_UI,
    RPG_TEXT_PRESET_NOTIFICATION,
    RPG_TEXT_PRESET_COUNT
} RpgTextPreset;

typedef struct RpgTextStyle {
    RpgTextRenderer renderer;
    float baseSize;
    Color color;
    /* Logical-pixel stroke expansion.  Zero preserves the native font weight;
     * positive values add one same-colour pass without changing layout size. */
    float strokeWidth;
} RpgTextStyle;

typedef struct RpgTextPresetSet {
    RpgTextStyle styles[RPG_TEXT_PRESET_COUNT];
} RpgTextPresetSet;

void RpgText_Draw(const RpgTextRenderer *renderer, const char *text,
                  Vector2 position, float size, Color color);
Vector2 RpgText_Measure(const RpgTextRenderer *renderer, const char *text, float size);

void RpgText_DrawPreset(const RpgTextPresetSet *presetSet, RpgTextPreset preset,
                        const char *text, Vector2 position, float sizeScale);
Vector2 RpgText_MeasurePreset(const RpgTextPresetSet *presetSet, RpgTextPreset preset,
                               const char *text, float sizeScale);
float RpgText_GetPresetScale(const RpgTextPresetSet *presetSet, RpgTextPreset preset,
                             float requestedSize);

/* Temporary adapter for older callers.  It is deliberately centralized so
 * remaining legacy colour/size call sites still render through a preset. */
RpgTextPreset RpgText_PresetFromLegacyColor(Color color);
void RpgText_DrawLegacy(const RpgTextPresetSet *presetSet, const char *text,
                        Vector2 position, float requestedSize, Color legacyColor);
Vector2 RpgText_MeasureLegacy(const RpgTextPresetSet *presetSet, const char *text,
                               float requestedSize);

#endif
