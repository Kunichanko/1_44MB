// 依存する自プロジェクト内ファイル: なし。
// 役割: Windows の DPI・システム色・Segoe 系フォントを取得し、Explorer UI 共通のメトリクスと描画素材を提供する。
#ifndef RPG_EXPLORER_THEME_H
#define RPG_EXPLORER_THEME_H

#include "raylib.h"

typedef struct ExplorerMetrics {
    /* すべて 96 DPI を基準にした論理ピクセル。 */
    float dpiScale;
    float titleBarHeight;
    float tabBarHeight;
    float navigationBarHeight;
    float commandBarHeight;
    float addressBarHeight;
    float searchBoxHeight;
    float navPaneWidth;
    float navRowHeight;
    float detailsHeaderHeight;
    float detailsRowHeight;
    float smallIconSize;
    float fileIconSize;
    float buttonSize;
    float buttonHeight;
    float horizontalPadding;
    float itemGap;
    float cornerRadius;

    /* 既存 UI の移行用別名。新規コードは上記の領域別メトリクスを使用する。 */
    float titleTabBarHeight;
    float navigationAddressBarHeight;
    float contentHeaderHeight;
    float navigationPaneWidth;
    float commandButtonWidth;
    float commandButtonHeight;
    float iconSize;
    float rowHeight;
    float addressHeight;
    float searchWidth;
} ExplorerMetrics;

typedef struct RpgExplorerTheme {
    /* logicalMetrics に DPI を一度だけ適用した結果が metrics。 */
    ExplorerMetrics logicalMetrics;
    ExplorerMetrics metrics;
    float dpiScale;
    Font textFont;
    Font japaneseFont;
    Font iconFont;
    bool hasTextFont;
    bool hasJapaneseFont;
    bool hasIconFont;
    bool usesFluentIcons;
    Color windowBackground;
    Color chromeBackground;
    Color text;
    Color secondaryText;
    Color disabledText;
    Color separator;
    Color hover;
    Color selection;
    Color accent;
} RpgExplorerTheme;

bool RpgExplorerTheme_Load(RpgExplorerTheme *theme, void *nativeWindow);
void RpgExplorerTheme_Unload(RpgExplorerTheme *theme);
void RpgExplorerTheme_UpdateDpi(RpgExplorerTheme *theme, void *nativeWindow);
void RpgExplorerTheme_DrawText(const RpgExplorerTheme *theme, const char *text, Vector2 position, float size, Color color);
Vector2 RpgExplorerTheme_MeasureText(const RpgExplorerTheme *theme, const char *text, float size);
void RpgExplorerTheme_DrawIcon(const RpgExplorerTheme *theme, int codepoint, Rectangle bounds, Color color);

#endif
