// 依存する自プロジェクト内ファイル: rpg_explorer_theme.h
// 役割: Windows の DPI・実行時フォントを読み込み、Explorer UI 用の論理レイアウトと色を提供する。
#include "rpg_explorer_theme.h"

#include <math.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#include <windows.h>
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

static float Scale(float value, float dpiScale)
{
    return roundf(value * dpiScale);
}

static void BuildLogicalMetrics(ExplorerMetrics *metrics)
{
    /* Windows 11 Explorer の 96 DPI 基準。ネイティブのタイトルバーは raylib 側に任せる。 */
    *metrics = (ExplorerMetrics){
        .dpiScale = 1.0f,
        .titleBarHeight = 0.0f,
        .tabBarHeight = 40.0f,
        .navigationBarHeight = 48.0f,
        .commandBarHeight = 44.0f,
        .addressBarHeight = 32.0f,
        .searchBoxHeight = 32.0f,
        .navPaneWidth = 232.0f,
        .navRowHeight = 28.0f,
        .detailsHeaderHeight = 28.0f,
        .detailsRowHeight = 28.0f,
        .smallIconSize = 16.0f,
        .fileIconSize = 16.0f,
        .buttonSize = 36.0f,
        .buttonHeight = 32.0f,
        .horizontalPadding = 12.0f,
        .itemGap = 4.0f,
        .cornerRadius = 6.0f
    };
}

static void ApplyDpi(ExplorerMetrics *destination, const ExplorerMetrics *logical, float dpiScale)
{
    *destination = *logical;
    destination->dpiScale = 1.0f;
    destination->titleBarHeight = Scale(logical->titleBarHeight, dpiScale);
    destination->tabBarHeight = Scale(logical->tabBarHeight, dpiScale);
    destination->navigationBarHeight = Scale(logical->navigationBarHeight, dpiScale);
    destination->commandBarHeight = Scale(logical->commandBarHeight, dpiScale);
    destination->addressBarHeight = Scale(logical->addressBarHeight, dpiScale);
    destination->searchBoxHeight = Scale(logical->searchBoxHeight, dpiScale);
    destination->navPaneWidth = Scale(logical->navPaneWidth, dpiScale);
    destination->navRowHeight = Scale(logical->navRowHeight, dpiScale);
    destination->detailsHeaderHeight = Scale(logical->detailsHeaderHeight, dpiScale);
    destination->detailsRowHeight = Scale(logical->detailsRowHeight, dpiScale);
    destination->smallIconSize = Scale(logical->smallIconSize, dpiScale);
    destination->fileIconSize = Scale(logical->fileIconSize, dpiScale);
    destination->buttonSize = Scale(logical->buttonSize, dpiScale);
    destination->buttonHeight = Scale(logical->buttonHeight, dpiScale);
    destination->horizontalPadding = Scale(logical->horizontalPadding, dpiScale);
    destination->itemGap = Scale(logical->itemGap, dpiScale);
    destination->cornerRadius = Scale(logical->cornerRadius, dpiScale);
    /* 移行中の UI が使用する値も、ここでだけ同じ DPI 変換結果へ合わせる。 */
    destination->titleTabBarHeight = destination->titleBarHeight + destination->tabBarHeight;
    destination->navigationAddressBarHeight = destination->navigationBarHeight;
    destination->contentHeaderHeight = destination->detailsHeaderHeight;
    destination->navigationPaneWidth = destination->navPaneWidth;
    destination->commandButtonWidth = destination->buttonSize;
    destination->commandButtonHeight = destination->buttonHeight;
    destination->iconSize = destination->smallIconSize;
    destination->rowHeight = destination->detailsRowHeight;
    destination->addressHeight = destination->addressBarHeight;
    destination->searchWidth = Scale(248.0f, dpiScale);
}

static bool GetWindowsFontPath(const wchar_t *fileName, char *path, size_t pathSize)
{
#ifdef _WIN32
    wchar_t windowsPath[MAX_PATH], fullPath[MAX_PATH];
    if (GetWindowsDirectoryW(windowsPath, MAX_PATH) == 0 ||
        swprintf(fullPath, MAX_PATH, L"%ls\\Fonts\\%ls", windowsPath, fileName) < 0 ||
        GetFileAttributesW(fullPath) == INVALID_FILE_ATTRIBUTES) return false;
    return WideCharToMultiByte(CP_UTF8, 0, fullPath, -1, path, (int)pathSize, NULL, NULL) > 0;
#else
    (void)fileName; (void)path; (void)pathSize;
    return false;
#endif
}

static bool NeedsJapaneseFallback(const char *text)
{
    if (text == NULL) return false;
    for (; *text != '\0'; text++) if (((unsigned char)*text) >= 0x80U) return true;
    return false;
}

static Font TextFontFor(const RpgExplorerTheme *theme, const char *text, bool *loaded)
{
    if (theme != NULL && theme->hasJapaneseFont && NeedsJapaneseFallback(text)) {
        *loaded = true;
        return theme->japaneseFont;
    }
    if (theme != NULL && theme->hasTextFont) {
        *loaded = true;
        return theme->textFont;
    }
    *loaded = false;
    return GetFontDefault();
}

static Font LoadJapaneseFont(const char *path)
{
    /* UI 文言と日本語ファイル名に必要な文字だけを atlas 化し、? への置換と過大な atlas を防ぐ。 */
    const int count = (0x7E - 0x20 + 1) + (0x30FF - 0x3000 + 1) + (0x9FFF - 0x4E00 + 1);
    int *codepoints = MemAlloc((size_t)count * sizeof(*codepoints));
    Font font = { 0 };
    int output = 0;
    if (codepoints == NULL) return font;
    for (int codepoint = 0x20; codepoint <= 0x7E; codepoint++) codepoints[output++] = codepoint;
    for (int codepoint = 0x3000; codepoint <= 0x30FF; codepoint++) codepoints[output++] = codepoint;
    for (int codepoint = 0x4E00; codepoint <= 0x9FFF; codepoint++) codepoints[output++] = codepoint;
    font = LoadFontEx(path, 32, codepoints, count);
    MemFree(codepoints);
    return font;
}

void RpgExplorerTheme_UpdateDpi(RpgExplorerTheme *theme, void *nativeWindow)
{
    float dpiScale = 1.0f;
#ifdef _WIN32
    if (nativeWindow != NULL) {
        HDC device = GetDC((HWND)nativeWindow);
        if (device != NULL) {
            dpiScale = (float)GetDeviceCaps(device, LOGPIXELSX) / 96.0f;
            ReleaseDC((HWND)nativeWindow, device);
        }
    }
#endif
    if (dpiScale <= 0.0f) dpiScale = GetWindowScaleDPI().x;
    if (dpiScale <= 0.0f) dpiScale = 1.0f;
    theme->dpiScale = dpiScale;
    BuildLogicalMetrics(&theme->logicalMetrics);
    /* UI が使用する metrics は、ここで一回だけ物理ピクセルへ変換する。 */
    ApplyDpi(&theme->metrics, &theme->logicalMetrics, dpiScale);
}

bool RpgExplorerTheme_Load(RpgExplorerTheme *theme, void *nativeWindow)
{
    char textPath[MAX_PATH] = { 0 }, japanesePath[MAX_PATH] = { 0 }, iconPath[MAX_PATH] = { 0 };
    const int iconCodepoints[] = { 0xE710, 0xE72B, 0xE72A, 0xE74A, 0xE72C, 0xE721, 0xE8C6,
                                   0xE8C8, 0xE77F, 0xE8AC, 0xE72D, 0xE74D, 0xE8CB, 0xE8A9, 0xE712 };
    if (theme == NULL) return false;
    memset(theme, 0, sizeof(*theme));
    RpgExplorerTheme_UpdateDpi(theme, nativeWindow);

    /* Explorer ライトテーマを基準にした専用パレット。classic GetSysColor の面色は使用しない。 */
    theme->windowBackground = (Color){ 255, 255, 255, 255 };
    theme->chromeBackground = (Color){ 249, 249, 249, 255 };
    theme->text = (Color){ 31, 31, 31, 255 };
    theme->secondaryText = (Color){ 96, 94, 92, 255 };
    theme->disabledText = (Color){ 160, 160, 160, 255 };
    theme->separator = (Color){ 228, 228, 228, 255 };
    theme->hover = (Color){ 0, 0, 0, 10 };
    theme->selection = (Color){ 0, 120, 212, 30 };
    theme->accent = (Color){ 0, 103, 192, 255 };

    if (GetWindowsFontPath(L"segoeui.ttf", textPath, sizeof(textPath))) {
        theme->textFont = LoadFontEx(textPath, 32, NULL, 0);
        theme->hasTextFont = theme->textFont.texture.id != 0;
    }
    /* Segoe UI にない日本語を Windows 標準の Yu Gothic UI で補う。フォントは同梱しない。 */
    if (GetWindowsFontPath(L"YuGothM.ttc", japanesePath, sizeof(japanesePath))) {
        theme->japaneseFont = LoadJapaneseFont(japanesePath);
        theme->hasJapaneseFont = theme->japaneseFont.texture.id != 0;
    }
    /* Fluent を優先し、未搭載の Windows では MDL2 を互換フォールバックとして使う。 */
    if (GetWindowsFontPath(L"Segoe Fluent Icons.ttf", iconPath, sizeof(iconPath))) theme->usesFluentIcons = true;
    else if (!GetWindowsFontPath(L"segmdl2.ttf", iconPath, sizeof(iconPath))) iconPath[0] = '\0';
    if (iconPath[0] != '\0') {
        theme->iconFont = LoadFontEx(iconPath, 32, iconCodepoints, (int)(sizeof(iconCodepoints) / sizeof(iconCodepoints[0])));
        theme->hasIconFont = theme->iconFont.texture.id != 0;
    }
    return theme->hasTextFont || theme->hasJapaneseFont;
}

void RpgExplorerTheme_Unload(RpgExplorerTheme *theme)
{
    if (theme == NULL) return;
    if (theme->hasTextFont) UnloadFont(theme->textFont);
    if (theme->hasJapaneseFont) UnloadFont(theme->japaneseFont);
    if (theme->hasIconFont) UnloadFont(theme->iconFont);
    memset(theme, 0, sizeof(*theme));
}

void RpgExplorerTheme_DrawText(const RpgExplorerTheme *theme, const char *text, Vector2 position, float size, Color color)
{
    bool loaded;
    Font font = TextFontFor(theme, text, &loaded);
    if (loaded) DrawTextEx(font, text, position, size, 0.0f, color);
    else DrawText(text, (int)position.x, (int)position.y, (int)size, color);
}

Vector2 RpgExplorerTheme_MeasureText(const RpgExplorerTheme *theme, const char *text, float size)
{
    bool loaded;
    Font font = TextFontFor(theme, text, &loaded);
    return loaded ? MeasureTextEx(font, text, size, 0.0f) : (Vector2){ (float)MeasureText(text, (int)size), size };
}

void RpgExplorerTheme_DrawIcon(const RpgExplorerTheme *theme, int codepoint, Rectangle bounds, Color color)
{
    int bytes = 0;
    const char *text = CodepointToUTF8(codepoint, &bytes);
    float glyphSize;
    Vector2 size;
    if (bytes <= 0 || text == NULL || theme == NULL || !theme->hasIconFont) return;
    /* ボタンの余白とは独立し、通常の command glyph は 16 logical px 相当だけ描画する。 */
    glyphSize = theme->metrics.smallIconSize;
    size = MeasureTextEx(theme->iconFont, text, glyphSize, 0.0f);
    DrawTextEx(theme->iconFont, text, (Vector2){ bounds.x + (bounds.width - size.x) * 0.5f,
                                                 bounds.y + (bounds.height - size.y) * 0.5f },
               glyphSize, 0.0f, color);
}
