// 依存する自プロジェクト内ファイル: rpg_explorer_theme.h
// 役割: Windows の DPI・実行時フォントを読み込み、Explorer UI 用の論理レイアウトと色を提供する。
#include "rpg_explorer_theme.h"

#include <stdio.h>
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

static void BuildLogicalMetrics(ExplorerMetrics *metrics)
{
    /* Windows 11 Explorer の 96 DPI 基準。ネイティブのタイトルバーは raylib 側に任せる。 */
    *metrics = (ExplorerMetrics){
        .dpiScale = 1.0f,
        .titleBarHeight = 0.0f,
        .tabBarHeight = 34.0f,
        .navigationBarHeight = 49.0f,
        .statusBarHeight = 28.0f,
        /* 個別button幅は公開APIで取得不能のため、32pxタイトル行へ収めるための暫定logical値。 */
        .captionButtonWidth = 46.0f,
        .captionButtonHeight = 32.0f,
        .captionGlyphSize = 12.0f,
        /* 168 DPI の UIA 実測を 96 DPI 論理値へ換算したナビゲーション基準。 */
        .navigationButtonSize = 32.0f,
        .navigationButtonGap = 16.0f,
        .navigationButtonLeftPadding = 10.9f,
        .navigationButtonTopPadding = 7.4f,
        .navigationRefreshAddressGap = 14.3f,
        /* M-SS の実線は約 12px だが、Segoe Fluent の 16px em-box 内に収まる。 */
        .navigationGlyphLayoutSize = 20.0f,
        .navigationGlyphFontSize = 16.0f,
        .addressTopPadding = 8.0f,
        .addressSearchGap = 8.0f,
        /* 以下は実測 N/A のため、従来の表示値を変更せず Metrics 化する。 */
        .addressBreadcrumbPadding = 14.0f,
        .breadcrumbRootIconSize = 20.0f,
        .breadcrumbRootIconTopPadding = 6.0f,
        .breadcrumbRootIconGlyphSize = 16.0f,
        .breadcrumbRootIconTextGap = 8.0f,
        .breadcrumbTextSize = 15.0f,
        .breadcrumbTextTopPadding = 7.0f,
        /* M-SS: Chevron の実線外接は約 4.6 x 8.6 logical px。 */
        .breadcrumbChevronVisualWidth = 4.6f,
        .breadcrumbChevronVisualHeight = 8.6f,
        /* 線幅は未測定のため従来値を維持する。 */
        .breadcrumbChevronStrokeWidth = 1.3f,
        .breadcrumbChevronSlotWidth = 14.0f,
        .breadcrumbAfterTextGap = 13.0f,
        .commandBarHeight = 46.0f,
        .commandLeftPadding = 12.0f,
        .commandButtonTopPadding = 6.0f,
        .commandIconOnlyButtonWidth = 40.0f,
        .commandIconOnlyButtonHeight = 32.0f,
        .commandButtonGap = 8.0f,
        .commandIconSize = 16.0f,
        /* Text/icon/chevron gaps are not measured; the previous visual spacing is retained. */
        .commandLabelHorizontalPadding = 12.0f,
        .commandLabelIconTextGap = 8.0f,
        .commandLabelChevronGap = 8.0f,
        .commandChevronSize = 12.0f,
        .commandTextSize = 14.0f,
        .commandSeparatorBefore = 8.0f,
        .commandSeparatorAfter = 12.0f,
        .commandSeparatorVerticalInset = 7.0f,
        .addressBarHeight = 32.0f,
        .searchBoxHeight = 32.0f,
        /* 小ウィンドウの実測約166pxを下限にし、残り領域をAddress Barと分配する。 */
        .searchBoxMinimumWidth = 166.0f,
        .searchBoxFlexibleShare = 0.29f,
        .navPaneWidth = 173.0f,
        .navRowHeight = 32.0f,
        .navTreeTopPadding = 5.0f,
        .navTreeSelectionHorizontalInset = 2.5f,
        .navTreeLevelIndent = 8.0f,
        .navTreeChevronLeft = 8.0f,
        .navTreeChevronHitWidth = 20.0f,
        .navTreeChevronGlyphSize = 12.0f,
        .navTreeIconLeft = 35.0f,
        .navTreeIconTextGap = 4.0f,
        .detailsHeaderHeight = 28.0f,
        .detailsRowHeight = 29.0f,
        .detailsRowActualHeight = 25.0f,
        .detailsRowTopInset = 2.0f,
        .detailsRowHorizontalInset = 6.0f,
        .detailsHeaderTextTopPadding = 7.0f,
        .detailsRowTextTopPadding = 11.0f,
        .detailsNameColumnWidth = 273.0f,
        .detailsDateColumnWidth = 144.0f,
        .detailsTypeColumnWidth = 120.0f,
        .detailsSizeColumnWidth = 80.0f,
        .detailsNameHeaderLeftPadding = 32.0f,
        .detailsColumnHeaderLeftPadding = 7.0f,
        .detailsNameIconLeftPadding = 21.0f,
        .detailsNameTextLeftPadding = 40.0f,
        .detailsSizeRightPadding = 7.0f,
        /* M-SS at 175%: operation area 30 physical px (17.1 logical), visible thumb
           about 7 physical px (4 logical), and about 11 physical px right margin (6 logical). */
        .detailsScrollbarOperationWidth = 17.0f,
        .detailsScrollbarThumbWidth = 4.0f,
        .detailsScrollbarRightMargin = 6.0f,
        /* The minimum is supplied from the active Windows metrics at runtime. */
        .detailsScrollbarMinimumThumbHeight = 0.0f,
        /* M-SS at 175%: item text begins about 24 physical px from the left;
           the two right controls are 46x45 physical px with a 5 physical px gap. */
        .statusTextLeftPadding = 14.0f,
        .statusTextSize = 13.0f,
        .statusViewButtonSize = 26.0f,
        .statusViewButtonGap = 3.0f,
        .statusViewGlyphSize = 16.0f,
        .navigationTextSize = 16.0f,
        .detailsTextSize = 16.0f,
        .smallIconSize = 16.0f,
        .fileIconSize = 16.0f,
        .buttonSize = 36.0f,
        .buttonHeight = 32.0f,
        .horizontalPadding = 12.0f,
        .itemGap = 4.0f,
        .cornerRadius = 6.0f
    };
}

#if 0
static void ApplyDpi(ExplorerMetrics *destination, const ExplorerMetrics *logical, float dpiScale)
{
    *destination = *logical;
    destination->dpiScale = 1.0f;
    destination->titleBarHeight = Scale(logical->titleBarHeight, dpiScale);
    destination->tabBarHeight = Scale(logical->tabBarHeight, dpiScale);
    destination->navigationBarHeight = Scale(logical->navigationBarHeight, dpiScale);
    destination->statusBarHeight = Scale(logical->statusBarHeight, dpiScale);
    destination->navigationButtonSize = Scale(logical->navigationButtonSize, dpiScale);
    destination->navigationButtonGap = Scale(logical->navigationButtonGap, dpiScale);
    destination->navigationButtonLeftPadding = Scale(logical->navigationButtonLeftPadding, dpiScale);
    destination->navigationButtonTopPadding = Scale(logical->navigationButtonTopPadding, dpiScale);
    destination->navigationRefreshAddressGap = Scale(logical->navigationRefreshAddressGap, dpiScale);
    destination->navigationGlyphLayoutSize = Scale(logical->navigationGlyphLayoutSize, dpiScale);
    destination->navigationGlyphFontSize = Scale(logical->navigationGlyphFontSize, dpiScale);
    destination->addressTopPadding = Scale(logical->addressTopPadding, dpiScale);
    destination->addressSearchGap = Scale(logical->addressSearchGap, dpiScale);
    destination->addressBreadcrumbPadding = Scale(logical->addressBreadcrumbPadding, dpiScale);
    destination->breadcrumbRootIconSize = Scale(logical->breadcrumbRootIconSize, dpiScale);
    destination->breadcrumbRootIconTopPadding = Scale(logical->breadcrumbRootIconTopPadding, dpiScale);
    destination->breadcrumbRootIconGlyphSize = Scale(logical->breadcrumbRootIconGlyphSize, dpiScale);
    destination->breadcrumbRootIconTextGap = Scale(logical->breadcrumbRootIconTextGap, dpiScale);
    destination->breadcrumbTextSize = Scale(logical->breadcrumbTextSize, dpiScale);
    destination->breadcrumbTextTopPadding = Scale(logical->breadcrumbTextTopPadding, dpiScale);
    destination->breadcrumbChevronVisualWidth = Scale(logical->breadcrumbChevronVisualWidth, dpiScale);
    destination->breadcrumbChevronVisualHeight = Scale(logical->breadcrumbChevronVisualHeight, dpiScale);
    destination->breadcrumbChevronStrokeWidth = Scale(logical->breadcrumbChevronStrokeWidth, dpiScale);
    destination->breadcrumbChevronSlotWidth = Scale(logical->breadcrumbChevronSlotWidth, dpiScale);
    destination->breadcrumbAfterTextGap = Scale(logical->breadcrumbAfterTextGap, dpiScale);
    destination->commandBarHeight = Scale(logical->commandBarHeight, dpiScale);
    destination->addressBarHeight = Scale(logical->addressBarHeight, dpiScale);
    destination->searchBoxHeight = Scale(logical->searchBoxHeight, dpiScale);
    destination->searchBoxMinimumWidth = Scale(logical->searchBoxMinimumWidth, dpiScale);
    destination->searchBoxFlexibleShare = logical->searchBoxFlexibleShare;
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
}
#endif

static void FinalizeLogicalMetrics(ExplorerMetrics *metrics)
{
    /* raylib の UI 座標は logical px。Metrics は変換せずに描画へ渡す。 */
    metrics->dpiScale = 1.0f;
    metrics->titleTabBarHeight = metrics->titleBarHeight + metrics->tabBarHeight;
    metrics->navigationAddressBarHeight = metrics->navigationBarHeight;
    metrics->contentHeaderHeight = metrics->detailsHeaderHeight;
    metrics->navigationPaneWidth = metrics->navPaneWidth;
    metrics->commandButtonWidth = metrics->buttonSize;
    metrics->commandButtonHeight = metrics->buttonHeight;
    metrics->iconSize = metrics->smallIconSize;
    metrics->rowHeight = metrics->detailsRowHeight;
    metrics->addressHeight = metrics->addressBarHeight;
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

static bool FontHasLoadedGlyph(Font font, int codepoint)
{
    int index = GetGlyphIndex(font, codepoint);
    return index >= 0 && index < font.glyphCount && font.glyphs[index].value == codepoint;
}

/* 英数字は Windows UI と同じ Segoe 系、日本語は必要な Glyph だけを持つ atlas を使う。 */
static bool UseJapaneseFontForCodepoint(const RpgExplorerTheme *theme, int codepoint)
{
    return codepoint > 0x7f && theme != NULL && theme->hasJapaneseFont &&
           FontHasLoadedGlyph(theme->japaneseFont, codepoint);
}

static Font FontForTextRun(const RpgExplorerTheme *theme, bool japanese, bool *loaded)
{
    if (japanese && theme != NULL && theme->hasJapaneseFont) {
        *loaded = true;
        return theme->japaneseFont;
    }
    if (theme != NULL && theme->hasTextFont) {
        *loaded = true;
        return theme->textFont;
    }
    if (theme != NULL && theme->hasJapaneseFont) {
        *loaded = true;
        return theme->japaneseFont;
    }
    *loaded = false;
    return GetFontDefault();
}

static Vector2 DrawOrMeasureTextRuns(const RpgExplorerTheme *theme, const char *text,
                                     Vector2 position, float size, Color color, bool draw)
{
    const char *runStart;
    const char *cursor;
    Vector2 result = { 0.0f, 0.0f };
    if (text == NULL || text[0] == '\0') return result;

    runStart = text;
    cursor = text;
    while (*runStart != '\0') {
        int firstBytes = 0;
        int firstCodepoint = GetCodepointNext(runStart, &firstBytes);
        bool japanese = UseJapaneseFontForCodepoint(theme, firstCodepoint);
        const char *runEnd = runStart;
        bool loaded;
        Font font;
        char *run;
        Vector2 runSize;
        if (firstBytes <= 0) break;
        while (*runEnd != '\0') {
            int byteCount = 0;
            int codepoint = GetCodepointNext(runEnd, &byteCount);
            if (byteCount <= 0 || UseJapaneseFontForCodepoint(theme, codepoint) != japanese) break;
            runEnd += byteCount;
        }
        run = (char *)MemAlloc((size_t)(runEnd - runStart) + 1U);
        if (run == NULL) break;
        memcpy(run, runStart, (size_t)(runEnd - runStart));
        run[runEnd - runStart] = '\0';
        font = FontForTextRun(theme, japanese, &loaded);
        runSize = loaded ? MeasureTextEx(font, run, size, 0.0f)
                         : (Vector2){ (float)MeasureText(run, (int)size), size };
        if (draw) {
            if (loaded) DrawTextEx(font, run, (Vector2){ position.x + result.x, position.y }, size, 0.0f, color);
            else DrawText(run, (int)(position.x + result.x), (int)position.y, (int)size, color);
        }
        result.x += runSize.x;
        if (runSize.y > result.y) result.y = runSize.y;
        MemFree(run);
        cursor = runEnd;
        runStart = cursor;
    }
    return result;
}

static Font LoadJapaneseFont(const char *path)
{
    {
        /* Fixed Explorer labels only. Dynamic text is added by RpgExplorerTheme_EnsureGlyphs(). */
        static const int baseCodepoints[] = {
            0x20, 0x5A, 0x69, 0x70, 0x65, 0x72,
            0x306E, 0x691C, 0x7D22, 0x65B0, 0x898F, 0x4F5C, 0x6210,
            0x4E26, 0x3079, 0x66FF, 0x3048, 0x8868, 0x793A, 0x540D, 0x524D,
            0x66F4, 0x65E5, 0x6642, 0x7A2E, 0x985E, 0x30B5, 0x30A4, 0x30BA,
            0x79FB, 0x52D5, 0x3057, 0x307E, 0x305F, 0x500B, 0x9805, 0x76EE
        };
        Font font = LoadFontEx(path, 32, baseCodepoints,
                               (int)(sizeof(baseCodepoints) / sizeof(baseCodepoints[0])));
        if (font.texture.id == 0) return font;
        for (int index = 0; index < (int)(sizeof(baseCodepoints) / sizeof(baseCodepoints[0])); index++) {
            if (!FontHasLoadedGlyph(font, baseCodepoints[index])) {
                UnloadFont(font);
                return (Font){ 0 };
            }
        }
        return font;
    }

#if 0

    /* 固定UI文言だけをatlasへ入れ、CJK全域の20,000字を展開しない。 */
    static const char *const fixedUiText[] = {
        "Zipper の検索", "新規作成", "並べ替え", "表示", "名前", "更新日時", "種類", "サイズ",
        "移動しました", "個の項目"
    };
    int codepoints[96];
    Font font = { 0 };
    int count = 0;
    for (int textIndex = 0; textIndex < (int)(sizeof(fixedUiText) / sizeof(fixedUiText[0])); textIndex++) {
        int sourceCount = 0;
        int *source = LoadCodepoints(fixedUiText[textIndex], &sourceCount);
        if (source == NULL) return font;
        for (int index = 0; index < sourceCount; index++) {
            if (!AddCodepointOnce(codepoints, &count, (int)(sizeof(codepoints) / sizeof(codepoints[0])), source[index])) {
                UnloadCodepoints(source);
                return font;
            }
        }
        UnloadCodepoints(source);
    }
    font = LoadFontEx(path, 32, codepoints, count);
    if (font.texture.id == 0) return font;
    for (int index = 0; index < count; index++) {
        if (!FontHasLoadedGlyph(font, codepoints[index])) {
            UnloadFont(font);
            return (Font){ 0 };
        }
    }
    return font;
#endif
}

static bool CodepointExists(const int *codepoints, int count, int codepoint)
{
    for (int index = 0; index < count; index++) {
        if (codepoints[index] == codepoint) return true;
    }
    return false;
}

static bool AppendUniqueCodepoint(int **codepoints, int *count, int *capacity, int codepoint)
{
    int *resized;
    int newCapacity;
    if (CodepointExists(*codepoints, *count, codepoint)) return true;
    if (*count < *capacity) {
        (*codepoints)[(*count)++] = codepoint;
        return true;
    }
    newCapacity = *capacity > 0 ? *capacity * 2 : 64;
    resized = (int *)MemRealloc(*codepoints, (size_t)newCapacity * sizeof(*resized));
    if (resized == NULL) return false;
    *codepoints = resized;
    *capacity = newCapacity;
    (*codepoints)[(*count)++] = codepoint;
    return true;
}

static Font LoadJapaneseAtlas(const char *path, const int *codepoints, int count)
{
    Font font;
    if (path == NULL || path[0] == '\0' || codepoints == NULL || count <= 0) return (Font){ 0 };
    font = LoadFontEx(path, 32, codepoints, count);
    if (font.texture.id == 0) return font;
    for (int index = 0; index < count; index++) {
        if (!FontHasLoadedGlyph(font, codepoints[index])) {
            UnloadFont(font);
            return (Font){ 0 };
        }
    }
    return font;
}

static bool RememberLoadedGlyphs(RpgExplorerTheme *theme)
{
    int *codepoints = NULL;
    int count = 0;
    int capacity = 0;
    for (int index = 0; index < theme->japaneseFont.glyphCount; index++) {
        if (!AppendUniqueCodepoint(&codepoints, &count, &capacity, theme->japaneseFont.glyphs[index].value)) {
            MemFree(codepoints);
            return false;
        }
    }
    theme->loadedCodepoints = codepoints;
    theme->loadedCodepointCount = count;
    theme->loadedCodepointCapacity = capacity;
    return true;
}

bool RpgExplorerTheme_EnsureGlyphs(RpgExplorerTheme *theme, const char *const *texts, int textCount)
{
    int *pending = NULL;
    int pendingCount = 0;
    int pendingCapacity = 0;
    int *allCodepoints;
    int totalCount;
    Font replacement;

    if (theme == NULL || !theme->hasJapaneseFont || theme->japaneseFontPath[0] == '\0' || texts == NULL) return false;
    for (int textIndex = 0; textIndex < textCount; textIndex++) {
        int sourceCount = 0;
        int *source;
        if (texts[textIndex] == NULL || texts[textIndex][0] == '\0') continue;
        source = LoadCodepoints(texts[textIndex], &sourceCount);
        if (source == NULL) continue;
        for (int index = 0; index < sourceCount; index++) {
            int codepoint = source[index];
            if (!CodepointExists(theme->loadedCodepoints, theme->loadedCodepointCount, codepoint) &&
                !AppendUniqueCodepoint(&pending, &pendingCount, &pendingCapacity, codepoint)) {
                UnloadCodepoints(source);
                MemFree(pending);
                return false;
            }
        }
        UnloadCodepoints(source);
    }
    if (pendingCount == 0) {
        MemFree(pending);
        return false;
    }

    totalCount = theme->loadedCodepointCount + pendingCount;
    allCodepoints = (int *)MemAlloc((size_t)totalCount * sizeof(*allCodepoints));
    if (allCodepoints == NULL) {
        MemFree(pending);
        return false;
    }
    memcpy(allCodepoints, theme->loadedCodepoints, (size_t)theme->loadedCodepointCount * sizeof(*allCodepoints));
    memcpy(allCodepoints + theme->loadedCodepointCount, pending, (size_t)pendingCount * sizeof(*allCodepoints));
    MemFree(pending);

    replacement = LoadJapaneseAtlas(theme->japaneseFontPath, allCodepoints, totalCount);
    if (replacement.texture.id == 0) {
        MemFree(allCodepoints);
        return false;
    }
    UnloadFont(theme->japaneseFont);
    theme->japaneseFont = replacement;
    MemFree(theme->loadedCodepoints);
    theme->loadedCodepoints = allCodepoints;
    theme->loadedCodepointCount = totalCount;
    theme->loadedCodepointCapacity = totalCount;
    theme->glyphAtlasRebuildCount++;
    return true;
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
    theme->metrics = theme->logicalMetrics;
    FinalizeLogicalMetrics(&theme->metrics);
#ifdef _WIN32
    {
        typedef int (WINAPI *GetSystemMetricsForDpiFn)(int index, UINT dpi);
        FARPROC rawGetSystemMetricsForDpi = GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                            "GetSystemMetricsForDpi");
        GetSystemMetricsForDpiFn getSystemMetricsForDpi = NULL;
        if (rawGetSystemMetricsForDpi != NULL)
            memcpy(&getSystemMetricsForDpi, &rawGetSystemMetricsForDpi,
                   sizeof(getSystemMetricsForDpi));
        int physicalMinimum = getSystemMetricsForDpi != NULL
            ? getSystemMetricsForDpi(SM_CYVTHUMB, (UINT)(dpiScale * 96.0f + 0.5f))
            : GetSystemMetrics(SM_CYVTHUMB);
        /* Windows returns a physical scrollbar thumb minimum; layout remains logical px. */
        if (physicalMinimum > 0)
            theme->metrics.detailsScrollbarMinimumThumbHeight = (float)physicalMinimum / dpiScale;
    }
#endif
}

bool RpgExplorerTheme_Load(RpgExplorerTheme *theme, void *nativeWindow)
{
    char textPath[MAX_PATH] = { 0 }, japanesePath[MAX_PATH] = { 0 }, iconPath[MAX_PATH] = { 0 };
    const int iconCodepoints[] = { 0xE710, 0xE72B, 0xE72A, 0xE74A, 0xE72C, 0xE721, 0xE8C6,
                                   0xE8C8, 0xE77F, 0xE8AC, 0xE72D, 0xE74D, 0xE8CB, 0xE8A9, 0xE712,
                                   0xE70D, 0xE76C, 0xE921, 0xE922, 0xE923, 0xE8BB, 0xECA5 };
    if (theme == NULL) return false;
    memset(theme, 0, sizeof(*theme));
    RpgExplorerTheme_UpdateDpi(theme, nativeWindow);

    /* Explorer ライトテーマを基準にした専用パレット。classic GetSysColor の面色は使用しない。 */
    theme->windowBackground = (Color){ 255, 255, 255, 255 };
    theme->chromeBackground = (Color){ 249, 249, 249, 255 };
    /* 同一 DPI の実 Explorer screenshot から、本文は黒、補助文字は #6d6d6d を採用する。 */
    theme->text = (Color){ 0, 0, 0, 255 };
    theme->secondaryText = (Color){ 109, 109, 109, 255 };
    theme->disabledText = (Color){ 160, 160, 160, 255 };
    theme->separator = (Color){ 228, 228, 228, 255 };
    /* 実 Explorer の Navigation Pane 非アクティブ選択を同一 screenshot から採った色。 */
    theme->navigationHover = (Color){ 240, 240, 240, 255 };
    theme->navigationSelection = (Color){ 217, 217, 217, 255 };
    theme->navigationSelectionBorder = (Color){ 148, 148, 148, 255 };
    theme->navigationSelectionHover = (Color){ 204, 204, 204, 255 };
    theme->hover = (Color){ 0, 0, 0, 10 };
    theme->selection = (Color){ 0, 120, 212, 30 };
    theme->accent = (Color){ 0, 103, 192, 255 };

    if (!GetWindowsFontPath(L"SegUIVar.ttf", textPath, sizeof(textPath)))
        GetWindowsFontPath(L"segoeui.ttf", textPath, sizeof(textPath));
    if (textPath[0] != '\0') {
        theme->textFont = LoadFontEx(textPath, 32, NULL, 0);
        theme->hasTextFont = theme->textFont.texture.id != 0;
    }
    /* Segoe UI にない日本語を Windows 標準の Yu Gothic UI で補う。フォントは同梱しない。 */
    /* raylibはTTCを安定してatlas化できないため、Windows Fonts内の日本語TTFを優先する。 */
    {
        /* TTC は raylib で必要 face を指定できないため採用しない。利用可能な TTF だけを検証する。 */
        static const wchar_t *const japaneseFontFiles[] = { L"NotoSansJP-VF.ttf", L"yumin.ttf" };
        for (int index = 0; index < (int)(sizeof(japaneseFontFiles) / sizeof(japaneseFontFiles[0])); index++) {
            if (!GetWindowsFontPath(japaneseFontFiles[index], japanesePath, sizeof(japanesePath))) continue;
            theme->japaneseFont = LoadJapaneseFont(japanesePath);
            theme->hasJapaneseFont = theme->japaneseFont.texture.id != 0;
            if (!theme->hasJapaneseFont) continue;
            snprintf(theme->japaneseFontPath, sizeof(theme->japaneseFontPath), "%s", japanesePath);
            if (RememberLoadedGlyphs(theme)) break;
            UnloadFont(theme->japaneseFont);
            theme->japaneseFont = (Font){ 0 };
            theme->hasJapaneseFont = false;
            theme->japaneseFontPath[0] = '\0';
        }
    }
    /* Fluent を優先し、未搭載の Windows では MDL2 を互換フォールバックとして使う。 */
    if (GetWindowsFontPath(L"Segoe Fluent Icons.ttf", iconPath, sizeof(iconPath))) theme->usesFluentIcons = true;
    else if (!GetWindowsFontPath(L"segmdl2.ttf", iconPath, sizeof(iconPath))) iconPath[0] = '\0';
    if (iconPath[0] != '\0') {
        theme->iconFont = LoadFontEx(iconPath, 32, iconCodepoints, (int)(sizeof(iconCodepoints) / sizeof(iconCodepoints[0])));
        theme->hasIconFont = theme->iconFont.texture.id != 0;
        for (int index = 0; theme->hasIconFont && index < (int)(sizeof(iconCodepoints) / sizeof(iconCodepoints[0])); index++) {
            if (!FontHasLoadedGlyph(theme->iconFont, iconCodepoints[index])) {
                UnloadFont(theme->iconFont);
                theme->iconFont = (Font){ 0 };
                theme->hasIconFont = false;
            }
        }
    }
    return theme->hasTextFont || theme->hasJapaneseFont;
}

void RpgExplorerTheme_Unload(RpgExplorerTheme *theme)
{
    if (theme == NULL) return;
    if (theme->hasTextFont) UnloadFont(theme->textFont);
    if (theme->hasJapaneseFont) UnloadFont(theme->japaneseFont);
    if (theme->hasIconFont) UnloadFont(theme->iconFont);
    MemFree(theme->loadedCodepoints);
    memset(theme, 0, sizeof(*theme));
}

void RpgExplorerTheme_DrawText(const RpgExplorerTheme *theme, const char *text, Vector2 position, float size, Color color)
{
    DrawOrMeasureTextRuns(theme, text, position, size, color, true);
}

Vector2 RpgExplorerTheme_MeasureText(const RpgExplorerTheme *theme, const char *text, float size)
{
    return DrawOrMeasureTextRuns(theme, text, (Vector2){ 0.0f, 0.0f }, size, BLANK, false);
}

void RpgExplorerTheme_DrawIcon(const RpgExplorerTheme *theme, int codepoint, Rectangle bounds, Color color)
{
    if (theme == NULL) return;
    RpgExplorerTheme_DrawIconSized(theme, codepoint, bounds, theme->metrics.smallIconSize, color);
}

void RpgExplorerTheme_DrawIconSized(const RpgExplorerTheme *theme, int codepoint, Rectangle bounds, float glyphSize, Color color)
{
    int bytes = 0;
    const char *text = CodepointToUTF8(codepoint, &bytes);
    Vector2 size;
    if (bytes <= 0 || text == NULL || theme == NULL || !theme->hasIconFont || glyphSize <= 0.0f) return;
    /* bounds は配置枠、glyphSize は実際にフォントへ渡す em-box サイズとして分離する。 */
    size = MeasureTextEx(theme->iconFont, text, glyphSize, 0.0f);
    DrawTextEx(theme->iconFont, text, (Vector2){ bounds.x + (bounds.width - size.x) * 0.5f,
                                                 bounds.y + (bounds.height - size.y) * 0.5f },
               glyphSize, 0.0f, color);
}
