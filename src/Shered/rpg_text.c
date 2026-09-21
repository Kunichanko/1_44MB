#include "rpg_text.h"

#include <stddef.h>
#include <stdbool.h>

void RpgText_Draw(const RpgTextRenderer *renderer, const char *text,
                  Vector2 position, float size, Color color)
{
    if (renderer == NULL || renderer->draw == NULL || text == NULL) return;
    renderer->draw(renderer->context, text, position, size, color);
}

Vector2 RpgText_Measure(const RpgTextRenderer *renderer, const char *text, float size)
{
    if (renderer == NULL || renderer->measure == NULL || text == NULL) return (Vector2){ 0.0f, 0.0f };
    return renderer->measure(renderer->context, text, size);
}

static const RpgTextStyle *GetStyle(const RpgTextPresetSet *presetSet, RpgTextPreset preset)
{
    if (presetSet == NULL || preset < RPG_TEXT_PRESET_GAME || preset >= RPG_TEXT_PRESET_COUNT) return NULL;
    return &presetSet->styles[preset];
}

static bool SameRgb(Color left, Color right)
{
    return left.r == right.r && left.g == right.g && left.b == right.b;
}

void RpgText_DrawPreset(const RpgTextPresetSet *presetSet, RpgTextPreset preset,
                        const char *text, Vector2 position, float sizeScale)
{
    const RpgTextStyle *style = GetStyle(presetSet, preset);
    if (style == NULL || sizeScale <= 0.0f) return;
    RpgText_Draw(&style->renderer, text, position, style->baseSize * sizeScale, style->color);
    if (style->strokeWidth > 0.0f) {
        RpgText_Draw(&style->renderer, text,
                     (Vector2){ position.x + style->strokeWidth, position.y },
                     style->baseSize * sizeScale, style->color);
    }
}

Vector2 RpgText_MeasurePreset(const RpgTextPresetSet *presetSet, RpgTextPreset preset,
                               const char *text, float sizeScale)
{
    const RpgTextStyle *style = GetStyle(presetSet, preset);
    if (style == NULL || sizeScale <= 0.0f) return (Vector2){ 0.0f, 0.0f };
    return RpgText_Measure(&style->renderer, text, style->baseSize * sizeScale);
}

float RpgText_GetPresetScale(const RpgTextPresetSet *presetSet, RpgTextPreset preset,
                             float requestedSize)
{
    const RpgTextStyle *style = GetStyle(presetSet, preset);
    if (style == NULL || style->baseSize <= 0.0f || requestedSize <= 0.0f) return 0.0f;
    return requestedSize / style->baseSize;
}

RpgTextPreset RpgText_PresetFromLegacyColor(Color color)
{
    if (SameRgb(color, MAROON) || SameRgb(color, RED) || SameRgb(color, ORANGE) ||
        SameRgb(color, DARKGREEN)) return RPG_TEXT_PRESET_NOTIFICATION;
    if (SameRgb(color, BLACK) || SameRgb(color, DARKBLUE) || SameRgb(color, DARKGRAY) ||
        SameRgb(color, GRAY) || SameRgb(color, DARKPURPLE) || SameRgb(color, PURPLE) ||
        SameRgb(color, DARKBROWN) || SameRgb(color, BROWN)) return RPG_TEXT_PRESET_UI;
    return RPG_TEXT_PRESET_GAME;
}

void RpgText_DrawLegacy(const RpgTextPresetSet *presetSet, const char *text,
                        Vector2 position, float requestedSize, Color legacyColor)
{
    RpgTextPreset preset = RpgText_PresetFromLegacyColor(legacyColor);
    RpgText_DrawPreset(presetSet, preset, text, position,
                       RpgText_GetPresetScale(presetSet, preset, requestedSize));
}

Vector2 RpgText_MeasureLegacy(const RpgTextPresetSet *presetSet, const char *text,
                               float requestedSize)
{
    return RpgText_MeasurePreset(presetSet, RPG_TEXT_PRESET_GAME, text,
                                 RpgText_GetPresetScale(presetSet, RPG_TEXT_PRESET_GAME,
                                                        requestedSize));
}
