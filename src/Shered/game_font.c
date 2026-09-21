// 依存: game_font.h
#include "game_font.h"

#include <string.h>

static Font gameFont = {0};
static bool isGameFontLoaded = false;
static char gameFontFilePath[512] = {0};
static char supportedCharacters[65536] = {0};
static int textBatchDepth = 0;
static bool textBatchHasNewGlyph = false;

static const char *defaultSupportedCharacters =
    "RPGプロトタイプへようこそこの世界には三つのマップがありますキーでカメラモードを切り替えられます話しかける次へ"
    "話しかけるプロトタイプへようこそ世界には三つのマップがありますキーでカメラモードを切り替えられます次へ"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 "
    /* ASCIIだけでなく、インスペクター・ファイル選択・通知で頻出する
       日本語UI記号を初回atlasへ入れる。未登録の記号が ? の代替Glyphに
       置き換わるのを防ぎ、表示のたびにatlasを作り直す必要もなくす。 */
    " ./\\:;,.!?()[]{}<>_=+\"'`~@#$%^&*|"
    "、。・：；！？（）［］｛｝〈〉《》〔〕【】「」『』"
    "…‥〜～ー―−／＼＿％＃＋＝＊＆＠＄"
    "×÷←→↑↓↔↕↖↗↘↙▶▷▼▽▲△●○◎□■◇◆"
    "横スクロールゲームエディター主人公敵従属化追従中巡回操作不可移動範囲体または"
    "アクション再生時設定保存できました選択読込失敗見た目画像色間隔補間速度対象仮"
    "キャラクター停止開始編集中確認青緑橙紫近い未起動次回反映インスペクター"
    "をとのでクリックして見せません動作数値入力欄確定";

static bool ReloadGameFont(void)
{
    int glyphCount = 0;
    int *codepoints = LoadCodepoints(supportedCharacters, &glyphCount);
    Font loadedFont = LoadFontEx(gameFontFilePath, 32, codepoints, glyphCount);
    UnloadCodepoints(codepoints);
    if (loadedFont.texture.id == 0) return false;
    if (isGameFontLoaded) UnloadFont(gameFont);
    gameFont = loadedFont;
    isGameFontLoaded = true;
    SetTextureFilter(gameFont.texture, TEXTURE_FILTER_BILINEAR);
    return isGameFontLoaded;
}

bool GameFont_Load(const char *filePath)
{
    strncpy(gameFontFilePath, filePath, sizeof(gameFontFilePath) - 1);
    gameFontFilePath[sizeof(gameFontFilePath) - 1] = '\0';
    strncpy(supportedCharacters, defaultSupportedCharacters, sizeof(supportedCharacters) - 1);
    supportedCharacters[sizeof(supportedCharacters) - 1] = '\0';
    textBatchDepth = 0;
    textBatchHasNewGlyph = false;
    return ReloadGameFont();
}

void GameFont_BeginTextBatch(void)
{
    textBatchDepth++;
}

bool GameFont_EndTextBatch(void)
{
    if (textBatchDepth <= 0) return true;
    textBatchDepth--;
    if (textBatchDepth > 0 || !textBatchHasNewGlyph) return true;
    textBatchHasNewGlyph = false;
    return ReloadGameFont();
}

bool GameFont_AddText(const char *text)
{
    bool hasNewGlyph = false;
    const char *cursor = text;
    while (*cursor != '\0') {
        int byteCount = 0;
        GetCodepointNext(cursor, &byteCount);
        if (byteCount <= 0 || byteCount > 4) break;
        char codepoint[5] = { 0 };
        memcpy(codepoint, cursor, (size_t)byteCount);
        if (strstr(supportedCharacters, codepoint) == NULL) {
            size_t used = strlen(supportedCharacters);
            if (used + (size_t)byteCount >= sizeof(supportedCharacters)) return false;
            memcpy(supportedCharacters + used, codepoint, (size_t)byteCount + 1);
            hasNewGlyph = true;
        }
        cursor += byteCount;
    }
    if (!hasNewGlyph) return true;
    if (textBatchDepth > 0) {
        textBatchHasNewGlyph = true;
        return true;
    }
    return ReloadGameFont();
}

void GameFont_Unload(void)
{
    if (isGameFontLoaded) {
        UnloadFont(gameFont);
        gameFont = (Font){0};
        isGameFontLoaded = false;
    }
}

static void GameFont_DrawBackend(const void *context, const char *text,
                                 Vector2 position, float fontSize, Color color)
{
    (void)context;
    if (isGameFontLoaded) {
        DrawTextEx(gameFont, text, position, fontSize, 1.0f, color);
        return;
    }

    DrawText(text, (int)position.x, (int)position.y, (int)fontSize, color);
}

static Vector2 GameFont_MeasureBackend(const void *context, const char *text, float fontSize)
{
    (void)context;
    if (isGameFontLoaded) return MeasureTextEx(gameFont, text, fontSize, 1.0f);
    return (Vector2){ (float)MeasureText(text, (int)fontSize), fontSize };
}

RpgTextRenderer GameFont_GetTextRenderer(void)
{
    return (RpgTextRenderer){ .context = NULL, .draw = GameFont_DrawBackend,
                              .measure = GameFont_MeasureBackend };
}

RpgTextPresetSet GameFont_GetTextPresetSet(void)
{
    RpgTextRenderer renderer = GameFont_GetTextRenderer();
    return (RpgTextPresetSet){
        .styles = {
            [RPG_TEXT_PRESET_GAME] = { .renderer = renderer, .baseSize = 16.0f, .color = GREEN,
                                       .strokeWidth = 0.5f },
            [RPG_TEXT_PRESET_UI] = { .renderer = renderer, .baseSize = 16.0f, .color = BLACK,
                                     .strokeWidth = 0.5f },
            [RPG_TEXT_PRESET_NOTIFICATION] = { .renderer = renderer, .baseSize = 16.0f, .color = MAROON,
                                               .strokeWidth = 0.5f }
        }
    };
}

void GameFont_DrawPreset(RpgTextPreset preset, const char *text, float x, float y, float sizeScale)
{
    RpgTextPresetSet presetSet = GameFont_GetTextPresetSet();
    RpgText_DrawPreset(&presetSet, preset, text, (Vector2){ x, y }, sizeScale);
}

Vector2 GameFont_MeasurePreset(RpgTextPreset preset, const char *text, float sizeScale)
{
    RpgTextPresetSet presetSet = GameFont_GetTextPresetSet();
    return RpgText_MeasurePreset(&presetSet, preset, text, sizeScale);
}

float GameFont_GetPresetScale(RpgTextPreset preset, float requestedSize)
{
    RpgTextPresetSet presetSet = GameFont_GetTextPresetSet();
    return RpgText_GetPresetScale(&presetSet, preset, requestedSize);
}

void GameFont_Draw(const char *text, float x, float y, float fontSize, Color color)
{
    RpgTextPresetSet presetSet = GameFont_GetTextPresetSet();
    RpgText_DrawLegacy(&presetSet, text, (Vector2){ x, y }, fontSize, color);
}

Vector2 GameFont_MeasureText(const char *text, float fontSize)
{
    RpgTextPresetSet presetSet = GameFont_GetTextPresetSet();
    return RpgText_MeasureLegacy(&presetSet, text, fontSize);
}

void GameFont_DrawText(const char *text, int x, int y, int fontSize, Color color)
{
    GameFont_Draw(text, (float)x, (float)y, (float)fontSize, color);
}

int GameFont_MeasureTextPixels(const char *text, int fontSize)
{
    return (int)GameFont_MeasureText(text, (float)fontSize).x;
}
// 役割: 日本語を含むゲーム内文字の登録、計測、描画を共通提供する。
