// 依存する自プロジェクト内ファイル: なし。
// 役割: Windows の DPI・システム色・Segoe 系フォントを取得し、Explorer UI 共通のメトリクスと描画素材を提供する。
#ifndef RPG_EXPLORER_THEME_H
#define RPG_EXPLORER_THEME_H

#include "raylib.h"

typedef struct ExplorerMetrics {
    /* raylib のレイアウト値は常に 96 DPI 基準の論理 px として保持する。 */
    /* すべて 96 DPI を基準にした論理ピクセル。 */
    float dpiScale;
    float titleBarHeight;
    float tabBarHeight;
    float navigationBarHeight;
    float statusBarHeight;
    /* 自作caption buttonは、描画矩形とWM_NCHITTESTで共通利用する。 */
    float captionButtonWidth;
    float captionButtonHeight;
    float captionGlyphSize;
    /* Back / Forward / Up / Refresh と Address Bar 専用の論理メトリクス。 */
    float navigationButtonSize;
    float navigationButtonGap;
    float navigationButtonLeftPadding;
    float navigationButtonTopPadding;
    float navigationRefreshAddressGap;
    /* hitbox と分離した Navigation Glyph の配置枠・Segoe Fluent em-box。 */
    float navigationGlyphLayoutSize;
    float navigationGlyphFontSize;
    float addressTopPadding;
    float addressSearchGap;
    /* Breadcrumb の個別寸法は実測されていないため、既存表示値を一か所へ集約する。 */
    float addressBreadcrumbPadding;
    float breadcrumbRootIconSize;
    float breadcrumbRootIconTopPadding;
    float breadcrumbRootIconGlyphSize;
    float breadcrumbRootIconTextGap;
    float breadcrumbTextSize;
    float breadcrumbTextTopPadding;
    float breadcrumbChevronVisualWidth;
    float breadcrumbChevronVisualHeight;
    float breadcrumbChevronStrokeWidth;
    float breadcrumbChevronSlotWidth;
    float breadcrumbAfterTextGap;
    float commandBarHeight;
    /* Command Bar は 168 DPI の Explorer 実測を 96 DPI logical px へ換算して管理する。 */
    float commandLeftPadding;
    float commandButtonTopPadding;
    float commandIconOnlyButtonWidth;
    float commandIconOnlyButtonHeight;
    float commandButtonGap;
    float commandIconSize;
    float commandLabelHorizontalPadding;
    float commandLabelIconTextGap;
    float commandLabelChevronGap;
    float commandChevronSize;
    float commandTextSize;
    float commandSeparatorBefore;
    float commandSeparatorAfter;
    float commandSeparatorVerticalInset;
    float addressBarHeight;
    float searchBoxHeight;
    /* Address/Search の残り幅を分配するための最小幅と flex 割合。 */
    float searchBoxMinimumWidth;
    float searchBoxFlexibleShare;
    float navPaneWidth;
    float navRowHeight;
    /* Navigation Pane 内部専用の tree row 構成。Pane 自体の幅とは独立する。 */
    float navTreeTopPadding;
    float navTreeSelectionHorizontalInset;
    float navTreeLevelIndent;
    float navTreeChevronLeft;
    float navTreeChevronHitWidth;
    float navTreeChevronGlyphSize;
    float navTreeIconLeft;
    float navTreeIconTextGap;
    float detailsHeaderHeight;
    float detailsRowHeight;
    float detailsRowActualHeight;
    float detailsRowTopInset;
    float detailsRowHorizontalInset;
    float detailsHeaderTextTopPadding;
    float detailsRowTextTopPadding;
    /* Details View は通常幅では、実Explorer測定値に基づく固定列幅を使う。 */
    float detailsNameColumnWidth;
    float detailsDateColumnWidth;
    float detailsTypeColumnWidth;
    float detailsSizeColumnWidth;
    float detailsNameHeaderLeftPadding;
    float detailsColumnHeaderLeftPadding;
    float detailsNameIconLeftPadding;
    float detailsNameTextLeftPadding;
    float detailsSizeRightPadding;
    /* Details View vertical scrollbar: operation area and visible thumb are separate. */
    float detailsScrollbarOperationWidth;
    float detailsScrollbarThumbWidth;
    float detailsScrollbarRightMargin;
    float detailsScrollbarMinimumThumbHeight;
    /* Status Bar measurements use the same 96-DPI logical coordinate system. */
    float statusTextLeftPadding;
    float statusTextSize;
    float statusViewButtonSize;
    float statusViewButtonGap;
    float statusViewGlyphSize;
    /* 文字の可視サイズ。行高・列幅とは独立した Typography 用の値。 */
    float navigationTextSize;
    float detailsTextSize;
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
} ExplorerMetrics;

typedef struct RpgExplorerTheme {
    /* logicalMetrics に DPI を一度だけ適用した結果が metrics。 */
    ExplorerMetrics logicalMetrics;
    ExplorerMetrics metrics;
    float dpiScale;
    Font textFont;
    Font japaneseFont;
    Font iconFont;
    /* Windows Fontsから実行時に生成する日本語atlasの累積Glyph集合。 */
    char japaneseFontPath[1024];
    int *loadedCodepoints;
    int loadedCodepointCount;
    int loadedCodepointCapacity;
    int glyphAtlasRebuildCount;
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
    Color navigationHover;
    Color navigationSelection;
    Color navigationSelectionBorder;
    Color navigationSelectionHover;
    Color hover;
    Color selection;
    Color accent;
} RpgExplorerTheme;

bool RpgExplorerTheme_Load(RpgExplorerTheme *theme, void *nativeWindow);
void RpgExplorerTheme_Unload(RpgExplorerTheme *theme);
void RpgExplorerTheme_UpdateDpi(RpgExplorerTheme *theme, void *nativeWindow);
bool RpgExplorerTheme_EnsureGlyphs(RpgExplorerTheme *theme, const char *const *texts, int textCount);
void RpgExplorerTheme_DrawText(const RpgExplorerTheme *theme, const char *text, Vector2 position, float size, Color color);
Vector2 RpgExplorerTheme_MeasureText(const RpgExplorerTheme *theme, const char *text, float size);
void RpgExplorerTheme_DrawIcon(const RpgExplorerTheme *theme, int codepoint, Rectangle bounds, Color color);
void RpgExplorerTheme_DrawIconSized(const RpgExplorerTheme *theme, int codepoint, Rectangle bounds, float glyphSize, Color color);

#endif
