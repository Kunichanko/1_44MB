// 依存: game_font.h
#include "game_font.h"

static Font gameFont = {0};
static bool isGameFontLoaded = false;

static const char *supportedCharacters =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 "
    "./:()×←→+-!?"
    "横スクロールゲームエディター主人公敵従属化追従中巡回操作不可移動範囲体または"
    "アクション再生時設定保存できました選択読込失敗見た目画像色間隔補間速度対象仮"
    "キャラクター停止開始編集中確認青緑橙紫近い未起動次回反映インスペクター"
    "をとのでクリックして見せません動作数値入力欄確定";

bool GameFont_Load(const char *filePath)
{
    int glyphCount = 0;
    int *codepoints = LoadCodepoints(supportedCharacters, &glyphCount);
    gameFont = LoadFontEx(filePath, 32, codepoints, glyphCount);
    UnloadCodepoints(codepoints);

    isGameFontLoaded = gameFont.texture.id != 0;
    if (isGameFontLoaded) {
        SetTextureFilter(gameFont.texture, TEXTURE_FILTER_BILINEAR);
    }
    return isGameFontLoaded;
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
