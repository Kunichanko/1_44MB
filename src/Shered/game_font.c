// 依存: game_font.h
#include "game_font.h"

#include <string.h>

static Font gameFont = {0};
static bool isGameFontLoaded = false;
static char gameFontFilePath[512] = {0};
static char supportedCharacters[65536] = {0};

static const char *defaultSupportedCharacters =
    "RPGプロトタイプへようこそこの世界には三つのマップがありますキーでカメラモードを切り替えられます話しかける次へ"
    "話しかけるプロトタイプへようこそ世界には三つのマップがありますキーでカメラモードを切り替えられます次へ"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 "
    "./:()×←→+-!?"
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
    return !hasNewGlyph || ReloadGameFont();
}

void GameFont_Unload(void)
{
    if (isGameFontLoaded) {
        UnloadFont(gameFont);
        gameFont = (Font){0};
        isGameFontLoaded = false;
    }
}

void GameFont_Draw(const char *text, float x, float y, float fontSize, Color color)
{
    if (isGameFontLoaded) {
        DrawTextEx(gameFont, text, (Vector2){ x, y }, fontSize, 1.0f, color);
        return;
    }

    DrawText(text, (int)x, (int)y, (int)fontSize, color);
}

Vector2 GameFont_MeasureText(const char *text, float fontSize)
{
    if (isGameFontLoaded) return MeasureTextEx(gameFont, text, fontSize, 1.0f);
    return (Vector2){ (float)MeasureText(text, (int)fontSize), fontSize };
}
