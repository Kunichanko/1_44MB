// 依存する自プロジェクト内ファイル: rpg_explorer_ui.h, rpg_explorer_window.h。
// 役割: ExplorerMetrics と Windows 標準素材を使い、Zipper 専用の Windows 11 Explorer 風 UI を描画・操作する。
#include "rpg_explorer_ui.h"
#include "rpg_explorer_window.h"

#include <stdio.h>
#include <string.h>

#include "raymath.h"

enum { ICON_NEW = 0xE710, ICON_BACK = 0xE72B, ICON_FORWARD = 0xE72A, ICON_UP = 0xE74A,
       ICON_REFRESH = 0xE72C, ICON_SEARCH = 0xE721, ICON_CUT = 0xE8C6, ICON_COPY = 0xE8C8,
       ICON_PASTE = 0xE77F, ICON_RENAME = 0xE8AC, ICON_SHARE = 0xE72D, ICON_DELETE = 0xE74D,
       ICON_SORT = 0xE8CB, ICON_VIEW = 0xE8A9, ICON_MORE = 0xE712, ICON_CHEVRON_DOWN = 0xE70D,
       ICON_CHEVRON_RIGHT = 0xE76C,
       ICON_TILES = 0xECA5,
       ICON_CAPTION_MINIMIZE = 0xE921, ICON_CAPTION_MAXIMIZE = 0xE922,
       ICON_CAPTION_RESTORE = 0xE923, ICON_CAPTION_CLOSE = 0xE8BB };

static float ContentTop(const ExplorerMetrics *m) { return m->titleTabBarHeight + m->navigationAddressBarHeight + m->commandBarHeight; }
static float ContentBottom(const ExplorerMetrics *m) { return (float)GetScreenHeight() - m->statusBarHeight; }
static void EnsureVisibleGlyphs(RpgExplorerUiState *state, const RpgExplorerFilesystem *fs, RpgExplorerTheme *theme)
{
    const char *texts[RPG_EXPLORER_MAX_ENTRIES * 2 + RPG_EXPLORER_MAX_TREE_NODES + 3];
    int textCount = 0;
    if (state == NULL || fs == NULL || theme == NULL) return;
    texts[textCount++] = RpgExplorerFilesystem_GetRelativePath(fs, fs->currentPath);
    texts[textCount++] = state->status;
    texts[textCount++] = "…";
    for (int index = 0; index < fs->treeNodeCount; index++) texts[textCount++] = fs->treeNodes[index].name;
    for (int index = 0; index < fs->entryCount; index++) {
        texts[textCount++] = fs->entries[index].name;
        texts[textCount++] = fs->entries[index].typeName;
    }
    RpgExplorerTheme_EnsureGlyphs(theme, texts, textCount);
    state->visibleGlyphsPrepared = true;
}

static void RefreshViewWithGlyphs(RpgExplorerUiState *state, RpgExplorerFilesystem *fs,
                                  RpgExplorerShellCache *cache, RpgExplorerTheme *theme)
{
    if (RpgExplorerFilesystem_Refresh(fs)) {
        RpgExplorerShell_ResolveEntries(cache, fs);
        EnsureVisibleGlyphs(state, fs, theme);
    }
}

static void RefreshView(RpgExplorerFilesystem *fs, RpgExplorerShellCache *cache)
{
    if (RpgExplorerFilesystem_Refresh(fs)) RpgExplorerShell_ResolveEntries(cache, fs);
}
static void Line(float x1, float y1, float x2, float y2, const RpgExplorerTheme *theme) { DrawLineEx((Vector2){x1,y1}, (Vector2){x2,y2}, 1.0f, theme->separator); }

/* raylib の logical 座標内で 1 framebuffer px 幅へ換算する Details Header 専用 hairline。 */
static void DetailsHeaderSeparator(float logicalX, float top, float height, const RpgExplorerTheme *theme)
{
    float dpiScale = theme->dpiScale > 0.0f ? theme->dpiScale : 1.0f;
    float x = roundf(logicalX * dpiScale) / dpiScale;
    float y = roundf(top * dpiScale) / dpiScale;
    float bottom = roundf((top + height) * dpiScale) / dpiScale;
    DrawRectangleRec((Rectangle){ x, y, 1.0f / dpiScale, fmaxf(0.0f, bottom - y) }, theme->separator);
}

static Rectangle CaptionButtonBounds(const RpgExplorerTheme *theme, RpgExplorerCaptionButton button)
{
    float buttonWidth = theme->metrics.captionButtonWidth;
    float width = (float)GetScreenWidth();
    float x = width - buttonWidth * 3.0f + buttonWidth * (float)button;
    return (Rectangle){ x, 0.0f, buttonWidth, theme->metrics.captionButtonHeight };
}

static void UpdateCaptionButtonLayout(const RpgExplorerTheme *theme)
{
    Rectangle minimize = CaptionButtonBounds(theme, RPG_EXPLORER_CAPTION_MINIMIZE);
    /* raylib と同じ logical client 幅を基準にし、Win32 hit test へ同じ矩形を渡す。 */
    RpgExplorerWindow_SetCaptionButtonLayout((Rectangle){ minimize.x, minimize.y,
                                                           minimize.width * 3.0f, minimize.height });
}

Rectangle RpgExplorerUi_GetTabBounds(void) { return (Rectangle){ 8.0f, 4.0f, 276.0f, 28.0f }; }

static void CommandButtonBackground(const RpgExplorerTheme *theme, Rectangle bounds, bool enabled)
{
    if (!enabled || !CheckCollisionPointRec(GetMousePosition(), bounds)) return;
    DrawRectangleRounded(bounds, 0.16f, 6,
                         IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? Fade(BLACK, 0.10f) : theme->hover);
}

static void IconButton(const RpgExplorerTheme *theme, Rectangle bounds, int icon, bool enabled)
{
    const ExplorerMetrics *m = &theme->metrics;
    Rectangle glyph = { bounds.x + (bounds.width - m->commandIconSize) * 0.5f,
                        bounds.y + (bounds.height - m->commandIconSize) * 0.5f,
                        m->commandIconSize, m->commandIconSize };
    CommandButtonBackground(theme, bounds, enabled);
    RpgExplorerTheme_DrawIconSized(theme, icon, glyph, m->commandIconSize,
                                   enabled ? theme->text : theme->disabledText);
}

static Rectangle NavigationButtonBounds(const ExplorerMetrics *m, float navTop, int index)
{
    const float stride = m->navigationButtonSize + m->navigationButtonGap;
    return (Rectangle){ m->navigationButtonLeftPadding + stride * (float)index,
                        navTop + m->navigationButtonTopPadding,
                        m->navigationButtonSize, m->navigationButtonSize };
}

static void NavigationIconButton(const RpgExplorerTheme *theme, Rectangle bounds, int icon, bool enabled)
{
    const ExplorerMetrics *m = &theme->metrics;
    float inset = (bounds.width - m->navigationGlyphLayoutSize) * 0.5f;
    Rectangle glyphLayout = { bounds.x + inset, bounds.y + inset,
                              m->navigationGlyphLayoutSize, m->navigationGlyphLayoutSize };
    if (enabled && CheckCollisionPointRec(GetMousePosition(), bounds)) DrawRectangleRounded(bounds, 0.16f, 6, theme->hover);
    RpgExplorerTheme_DrawIconSized(theme, icon, glyphLayout, m->navigationGlyphFontSize,
                                   enabled ? theme->text : theme->disabledText);
}

static void DrawCaptionButton(const RpgExplorerTheme *theme, Rectangle bounds,
                              RpgExplorerCaptionButton button, int icon, bool closeButton)
{
    bool hovered = RpgExplorerWindow_IsCaptionButtonHovered(button);
    bool pressed = RpgExplorerWindow_IsCaptionButtonPressed(button);
    Color glyph = theme->text;
    if (hovered) {
        if (closeButton) {
            DrawRectangleRec(bounds, pressed ? (Color){ 169, 36, 25, 255 } : (Color){ 196, 43, 28, 255 });
            glyph = RAYWHITE;
        } else {
            DrawRectangleRec(bounds, pressed ? Fade(BLACK, 0.12f) : Fade(BLACK, 0.06f));
        }
    }
    RpgExplorerTheme_DrawIconSized(theme, icon, bounds, theme->metrics.captionGlyphSize, glyph);
}

static void CaptionButtons(const RpgExplorerTheme *theme)
{
    UpdateCaptionButtonLayout(theme);
    Rectangle minimize = CaptionButtonBounds(theme, RPG_EXPLORER_CAPTION_MINIMIZE);
    Rectangle maximize = CaptionButtonBounds(theme, RPG_EXPLORER_CAPTION_MAXIMIZE);
    Rectangle closeButton = CaptionButtonBounds(theme, RPG_EXPLORER_CAPTION_CLOSE);
    if (minimize.width <= 0.0f || maximize.width <= 0.0f || closeButton.width <= 0.0f) return;
    DrawCaptionButton(theme, minimize, RPG_EXPLORER_CAPTION_MINIMIZE, ICON_CAPTION_MINIMIZE, false);
    DrawCaptionButton(theme, maximize, RPG_EXPLORER_CAPTION_MAXIMIZE,
                      RpgExplorerWindow_IsMaximized() ? ICON_CAPTION_RESTORE : ICON_CAPTION_MAXIMIZE, false);
    DrawCaptionButton(theme, closeButton, RPG_EXPLORER_CAPTION_CLOSE, ICON_CAPTION_CLOSE, true);
}

static void LabelButton(const RpgExplorerTheme *theme, Rectangle bounds, int icon, const char *label, bool enabled)
{
    const ExplorerMetrics *m = &theme->metrics;
    Rectangle iconBounds = { bounds.x + m->commandLabelHorizontalPadding,
                             bounds.y + (bounds.height - m->commandIconSize) * 0.5f,
                             m->commandIconSize, m->commandIconSize };
    Vector2 textPosition = { iconBounds.x + iconBounds.width + m->commandLabelIconTextGap,
                             bounds.y + (bounds.height - m->commandTextSize) * 0.5f };
    Vector2 textSize = RpgExplorerTheme_MeasureText(theme, label, m->commandTextSize);
    Rectangle chevronBounds = { textPosition.x + textSize.x + m->commandLabelChevronGap,
                                bounds.y + (bounds.height - m->commandChevronSize) * 0.5f,
                                m->commandChevronSize, m->commandChevronSize };
    CommandButtonBackground(theme, bounds, enabled);
    RpgExplorerTheme_DrawIconSized(theme, icon, iconBounds, m->commandIconSize,
                                   enabled ? theme->text : theme->disabledText);
    RpgExplorerTheme_DrawText(theme, label, textPosition, m->commandTextSize,
                               enabled ? theme->text : theme->disabledText);
    RpgExplorerTheme_DrawIconSized(theme, ICON_CHEVRON_DOWN, chevronBounds, m->commandChevronSize,
                                   enabled ? theme->secondaryText : theme->disabledText);
}

static float CommandLabelButtonWidth(const RpgExplorerTheme *theme, const char *label)
{
    const ExplorerMetrics *m = &theme->metrics;
    return m->commandLabelHorizontalPadding * 2.0f + m->commandIconSize +
           m->commandLabelIconTextGap + RpgExplorerTheme_MeasureText(theme, label, m->commandTextSize).x +
           m->commandLabelChevronGap + m->commandChevronSize;
}

static void BreadcrumbChevron(const ExplorerMetrics *m, Rectangle bounds, Color color)
{
    float x = bounds.x;
    float y = bounds.y + (bounds.height - m->breadcrumbChevronVisualHeight) * 0.5f;
    float midY = y + m->breadcrumbChevronVisualHeight * 0.5f;
    DrawLineEx((Vector2){ x, y }, (Vector2){ x + m->breadcrumbChevronVisualWidth, midY }, m->breadcrumbChevronStrokeWidth, color);
    DrawLineEx((Vector2){ x, y + m->breadcrumbChevronVisualHeight }, (Vector2){ x + m->breadcrumbChevronVisualWidth, midY }, m->breadcrumbChevronStrokeWidth, color);
}

enum { RPG_EXPLORER_MAX_BREADCRUMB_SEGMENTS = 24 };

typedef struct BreadcrumbSegment {
    char label[RPG_EXPLORER_NAME_LENGTH];
    char path[RPG_EXPLORER_PATH_LENGTH];
    Rectangle bounds;
    bool hasChevron;
    bool canNavigate;
} BreadcrumbSegment;

/* This preserves the current Address/Search flex geometry. Drawing and click
   handling deliberately share it, so Breadcrumbs cannot enter Search. */
static Rectangle AddressBarBounds(const ExplorerMetrics *m)
{
    float navTop = m->titleTabBarHeight;
    Rectangle refresh = NavigationButtonBounds(m, navTop, 3);
    float addressX = refresh.x + refresh.width + m->navigationRefreshAddressGap;
    float width = (float)GetScreenWidth();
    float flexibleWidth = width - m->horizontalPadding - m->addressSearchGap - addressX;
    float searchWidth = fmaxf(m->searchBoxMinimumWidth, flexibleWidth * m->searchBoxFlexibleShare);
    float searchX = width - searchWidth - m->horizontalPadding;
    float addressY = navTop + (m->navigationAddressBarHeight - m->addressHeight) * 0.5f;
    return (Rectangle){ addressX, addressY,
                        fmaxf(0.0f, searchX - m->addressSearchGap - addressX),
                        m->addressHeight };
}

static int BuildBreadcrumbSegments(const RpgExplorerFilesystem *fs,
                                   BreadcrumbSegment *segments, int capacity)
{
    char relative[RPG_EXPLORER_PATH_LENGTH];
    char currentPath[RPG_EXPLORER_PATH_LENGTH];
    char *part;
    int count = 0;
    if (fs == NULL || segments == NULL || capacity <= 0) return 0;
    memset(segments, 0, (size_t)capacity * sizeof(*segments));
    snprintf(segments[count].label, sizeof(segments[count].label), "Zipper");
    snprintf(segments[count].path, sizeof(segments[count].path), "%s", fs->rootPath);
    segments[count++].canNavigate = true;
    snprintf(relative, sizeof(relative), "%s",
             RpgExplorerFilesystem_GetRelativePath(fs, fs->currentPath));
    snprintf(currentPath, sizeof(currentPath), "%s", fs->rootPath);
    for (part = strtok(relative, "\\/"); part != NULL && count < capacity;
         part = strtok(NULL, "\\/")) {
        BreadcrumbSegment *segment = &segments[count];
        char nextPath[RPG_EXPLORER_PATH_LENGTH];
        int written = snprintf(nextPath, sizeof(nextPath), "%s\\%s", currentPath, part);
        if (written <= 0 || written >= (int)sizeof(nextPath)) break;
        snprintf(currentPath, sizeof(currentPath), "%s", nextPath);
        snprintf(segment->label, sizeof(segment->label), "%s", part);
        snprintf(segment->path, sizeof(segment->path), "%s", currentPath);
        segment->hasChevron = true;
        segment->canNavigate = true;
        count++;
    }
    return count;
}

static float BreadcrumbSegmentWidth(const RpgExplorerTheme *theme, const BreadcrumbSegment *segment)
{
    const ExplorerMetrics *m = &theme->metrics;
    float width = RpgExplorerTheme_MeasureText(theme, segment->label, m->breadcrumbTextSize).x +
                  m->breadcrumbAfterTextGap;
    return width + (segment->hasChevron ? m->breadcrumbChevronSlotWidth :
                    m->breadcrumbRootIconSize + m->breadcrumbRootIconTextGap);
}

static int BuildVisibleBreadcrumbSegments(const RpgExplorerTheme *theme,
                                          const RpgExplorerFilesystem *fs, Rectangle address,
                                          BreadcrumbSegment *visible, int capacity)
{
    BreadcrumbSegment all[RPG_EXPLORER_MAX_BREADCRUMB_SEGMENTS];
    const ExplorerMetrics *m = &theme->metrics;
    float available = fmaxf(0.0f, address.width - m->addressBreadcrumbPadding * 2.0f);
    float used = 0.0f;
    int allCount = BuildBreadcrumbSegments(fs, all, RPG_EXPLORER_MAX_BREADCRUMB_SEGMENTS);
    int count = 0;
    if (capacity <= 0 || allCount <= 0) return 0;
    for (int index = 0; index < allCount; index++) used += BreadcrumbSegmentWidth(theme, &all[index]);
    if (used <= available) {
        for (int index = 0; index < allCount && count < capacity; index++) visible[count++] = all[index];
        return count;
    }

    /* Preserve root + the current end. The ellipsis has its own rectangle but
       no path, so it cannot accidentally navigate outside Zipper. */
    visible[count++] = all[0];
    if (count < capacity) {
        BreadcrumbSegment *ellipsis = &visible[count++];
        memset(ellipsis, 0, sizeof(*ellipsis));
        snprintf(ellipsis->label, sizeof(ellipsis->label), "…");
        ellipsis->hasChevron = true;
    }
    used = BreadcrumbSegmentWidth(theme, &visible[0]) + BreadcrumbSegmentWidth(theme, &visible[1]);
    for (int index = allCount - 1; index > 0 && count < capacity; index--) {
        float width = BreadcrumbSegmentWidth(theme, &all[index]);
        if (used + width > available && index != allCount - 1) break;
        visible[count++] = all[index];
        used += width;
    }
    for (int left = 2, right = count - 1; left < right; left++, right--) {
        BreadcrumbSegment temporary = visible[left];
        visible[left] = visible[right];
        visible[right] = temporary;
    }
    return count;
}

static int LayoutBreadcrumbSegments(const RpgExplorerTheme *theme,
                                    const RpgExplorerFilesystem *fs, Rectangle address,
                                    BreadcrumbSegment *segments, int capacity)
{
    const ExplorerMetrics *m = &theme->metrics;
    float cursor = address.x + m->addressBreadcrumbPadding;
    float right = address.x + address.width - m->addressBreadcrumbPadding;
    int count = BuildVisibleBreadcrumbSegments(theme, fs, address, segments, capacity);
    for (int index = 0; index < count; index++) {
        float width = BreadcrumbSegmentWidth(theme, &segments[index]);
        if (cursor >= right) width = 0.0f;
        else if (cursor + width > right) width = right - cursor;
        segments[index].bounds = (Rectangle){ cursor, address.y, fmaxf(0.0f, width), address.height };
        cursor += width;
    }
    return count;
}

static bool BreadcrumbTargetAt(const RpgExplorerTheme *theme, const RpgExplorerFilesystem *fs,
                               Rectangle address, Vector2 point, char *path, int pathSize)
{
    BreadcrumbSegment segments[RPG_EXPLORER_MAX_BREADCRUMB_SEGMENTS];
    int count = LayoutBreadcrumbSegments(theme, fs, address, segments,
                                         RPG_EXPLORER_MAX_BREADCRUMB_SEGMENTS);
    for (int index = 0; index < count; index++) {
        if (segments[index].canNavigate && CheckCollisionPointRec(point, segments[index].bounds)) {
            snprintf(path, (size_t)pathSize, "%s", segments[index].path);
            return true;
        }
    }
    return false;
}

static void AddressBar(const RpgExplorerTheme *theme, const RpgExplorerFilesystem *fs, Rectangle bounds)
{
    const ExplorerMetrics *m = &theme->metrics;
    BreadcrumbSegment segments[RPG_EXPLORER_MAX_BREADCRUMB_SEGMENTS];
    Vector2 mouse = GetMousePosition();
    int count = LayoutBreadcrumbSegments(theme, fs, bounds, segments,
                                         RPG_EXPLORER_MAX_BREADCRUMB_SEGMENTS);
    DrawRectangleRounded(bounds, 0.18f, 8, Fade(theme->chromeBackground, 0.55f));
    DrawRectangleLinesEx(bounds, 1.0f, theme->separator);
    BeginScissorMode((int)ceilf(bounds.x + 1.0f), (int)ceilf(bounds.y + 1.0f),
                     (int)fmaxf(0.0f, floorf(bounds.width - 2.0f)),
                     (int)fmaxf(0.0f, floorf(bounds.height - 2.0f)));
    for (int index = 0; index < count; index++) {
        BreadcrumbSegment *segment = &segments[index];
        float labelX = segment->bounds.x;
        if (segment->canNavigate && CheckCollisionPointRec(mouse, segment->bounds))
            DrawRectangleRounded(segment->bounds, 0.16f, 6, theme->hover);
        if (segment->hasChevron) {
            BreadcrumbChevron(m, (Rectangle){ labelX, bounds.y, m->breadcrumbChevronSlotWidth, bounds.height },
                              theme->secondaryText);
            labelX += m->breadcrumbChevronSlotWidth;
        } else {
            RpgExplorerTheme_DrawIconSized(theme, ICON_VIEW,
                                           (Rectangle){ labelX, bounds.y + m->breadcrumbRootIconTopPadding,
                                                        m->breadcrumbRootIconSize, m->breadcrumbRootIconSize },
                                           m->breadcrumbRootIconGlyphSize, theme->secondaryText);
            labelX += m->breadcrumbRootIconSize + m->breadcrumbRootIconTextGap;
        }
        RpgExplorerTheme_DrawText(theme, segment->label,
                                  (Vector2){ labelX, bounds.y + m->breadcrumbTextTopPadding },
                                  m->breadcrumbTextSize, theme->text);
    }
    EndScissorMode();
}

static void Chrome(const RpgExplorerTheme *theme, const RpgExplorerFilesystem *fs)
{
    const ExplorerMetrics *m = &theme->metrics;
    float width = (float)GetScreenWidth(), tabHeight = m->titleTabBarHeight;
    Rectangle tab = RpgExplorerUi_GetTabBounds();
    RpgExplorerWindow_SetCaptionButtonWidth(m->captionButtonWidth);
    UpdateCaptionButtonLayout(theme);
    Rectangle captionButtons = CaptionButtonBounds(theme, RPG_EXPLORER_CAPTION_MINIMIZE);
    captionButtons.width *= 3.0f;
    if (captionButtons.width > 0.0f && tab.x + tab.width > captionButtons.x)
        tab.width = fmaxf(0.0f, captionButtons.x - tab.x);
    DrawRectangle(0,0,GetScreenWidth(),(int)tabHeight,theme->chromeBackground);
    DrawRectangleRounded(tab,0.18f,8,theme->windowBackground);
    RpgExplorerTheme_DrawIcon(theme,ICON_VIEW,(Rectangle){tab.x + 14.0f, tab.y + 10.0f, 18.0f, 18.0f},theme->accent);
    RpgExplorerTheme_DrawText(theme,"Zipper",(Vector2){tab.x + 43.0f, tab.y + 12.0f},16.0f,theme->text);
    if (captionButtons.width <= 0.0f || tab.x + tab.width + 48.0f < captionButtons.x)
        RpgExplorerTheme_DrawText(theme,"+",(Vector2){tab.x + tab.width + 24.0f, tab.y + 9.0f},24.0f,theme->secondaryText);
    CaptionButtons(theme);
    Line(0,tabHeight,width,tabHeight,theme);

    float navTop=tabHeight;
    DrawRectangle(0,(int)navTop,GetScreenWidth(),(int)m->navigationAddressBarHeight,theme->windowBackground);
    Rectangle back = NavigationButtonBounds(m, navTop, 0);
    Rectangle forward = NavigationButtonBounds(m, navTop, 1);
    Rectangle up = NavigationButtonBounds(m, navTop, 2);
    Rectangle refresh = NavigationButtonBounds(m, navTop, 3);
    NavigationIconButton(theme,back,ICON_BACK,true); NavigationIconButton(theme,forward,ICON_FORWARD,false); NavigationIconButton(theme,up,ICON_UP,_stricmp(fs->currentPath,fs->rootPath)!=0); NavigationIconButton(theme,refresh,ICON_REFRESH,true);
    Rectangle address = AddressBarBounds(m);
    float searchX = address.x + address.width + m->addressSearchGap;
    float searchWidth = width - searchX - m->horizontalPadding;
    AddressBar(theme,fs,address);
    Rectangle search={searchX,address.y,searchWidth,m->addressHeight};
    DrawRectangleRounded(search,0.18f,8,Fade(theme->chromeBackground,0.64f));
    DrawRectangleLinesEx(search,1.0f,theme->separator);
    RpgExplorerTheme_DrawText(theme,"Zipper の検索",(Vector2){search.x + 12.0f, search.y + 7.0f},15.0f,theme->secondaryText);
    RpgExplorerTheme_DrawIcon(theme,ICON_SEARCH,(Rectangle){search.x + search.width - 30.0f, search.y + 6.0f, 20.0f, 20.0f},theme->text);
    Line(0,navTop+m->navigationAddressBarHeight,width,navTop+m->navigationAddressBarHeight,theme);

    float cmdTop=tabHeight+m->navigationAddressBarHeight,cursor=m->commandLeftPadding;
    float commandButtonY = cmdTop + (m->commandBarHeight - m->commandIconOnlyButtonHeight) * 0.5f;
    DrawRectangle(0,(int)cmdTop,GetScreenWidth(),(int)m->commandBarHeight,theme->windowBackground);
    float newWidth = CommandLabelButtonWidth(theme, "新規作成");
    LabelButton(theme,(Rectangle){cursor,commandButtonY,newWidth,m->commandIconOnlyButtonHeight},ICON_NEW,"新規作成",false); cursor += newWidth + m->commandButtonGap;
    const int icons[]={ICON_CUT,ICON_COPY,ICON_PASTE,ICON_RENAME,ICON_SHARE,ICON_DELETE};
    for(int i=0;i<6;i++){IconButton(theme,(Rectangle){cursor,commandButtonY,m->commandIconOnlyButtonWidth,m->commandIconOnlyButtonHeight},icons[i],false);cursor += m->commandIconOnlyButtonWidth + (i < 5 ? m->commandButtonGap : 0.0f);}
    float separatorX = cursor + m->commandSeparatorBefore;
    Line(separatorX,cmdTop + m->commandSeparatorVerticalInset,separatorX,cmdTop + m->commandBarHeight - m->commandSeparatorVerticalInset,theme); cursor = separatorX + m->commandSeparatorAfter;
    float sortWidth = CommandLabelButtonWidth(theme, "並べ替え");
    LabelButton(theme,(Rectangle){cursor,commandButtonY,sortWidth,m->commandIconOnlyButtonHeight},ICON_SORT,"並べ替え",true);cursor += sortWidth + m->commandButtonGap;
    float viewWidth = CommandLabelButtonWidth(theme, "表示");
    LabelButton(theme,(Rectangle){cursor,commandButtonY,viewWidth,m->commandIconOnlyButtonHeight},ICON_VIEW,"表示",true);cursor += viewWidth + m->commandButtonGap;
    IconButton(theme,(Rectangle){cursor,commandButtonY,m->commandIconOnlyButtonWidth,m->commandIconOnlyButtonHeight},ICON_MORE,true);
    Line(0,cmdTop+m->commandBarHeight,width,cmdTop+m->commandBarHeight,theme);
}

static Rectangle DetailsRowBounds(const ExplorerMetrics *m, float contentLeft, float contentWidth, float rowTop)
{
    return (Rectangle){ contentLeft + m->detailsRowHorizontalInset,
                        rowTop + m->detailsRowTopInset,
                        contentWidth - m->detailsRowHorizontalInset * 2.0f,
                        m->detailsRowActualHeight };
}

typedef struct DetailsScrollLayout {
    Rectangle rowViewport;
    Rectangle operationBounds;
    Rectangle thumbBounds;
    int visibleRows;
    int maxScroll;
    bool hasOverflow;
} DetailsScrollLayout;

static DetailsScrollLayout GetDetailsScrollLayout(const ExplorerMetrics *m,
                                                  const RpgExplorerFilesystem *fs,
                                                  int scroll)
{
    float contentLeft = m->navigationPaneWidth;
    float rowsTop = ContentTop(m) + m->contentHeaderHeight;
    float rowsBottom = ContentBottom(m);
    float contentWidth = (float)GetScreenWidth() - contentLeft;
    DetailsScrollLayout layout = { 0 };
    float trackTravel;
    float normalizedScroll;

    layout.rowViewport = (Rectangle){ contentLeft, rowsTop, contentWidth,
                                      fmaxf(0.0f, rowsBottom - rowsTop) };
    layout.visibleRows = (int)floorf(layout.rowViewport.height / m->rowHeight);
    if (layout.visibleRows < 1) layout.visibleRows = 1;
    layout.maxScroll = fs->entryCount - layout.visibleRows;
    if (layout.maxScroll < 0) layout.maxScroll = 0;
    layout.hasOverflow = layout.maxScroll > 0;
    if (!layout.hasOverflow) return layout;

    layout.operationBounds = (Rectangle){ (float)GetScreenWidth() - m->detailsScrollbarOperationWidth,
                                           rowsTop, m->detailsScrollbarOperationWidth,
                                           layout.rowViewport.height };
    layout.rowViewport.width = fmaxf(0.0f, layout.operationBounds.x - contentLeft);
    layout.thumbBounds.width = m->detailsScrollbarThumbWidth;
    layout.thumbBounds.height = fmaxf(m->detailsScrollbarMinimumThumbHeight,
                                      layout.operationBounds.height *
                                      (float)layout.visibleRows / (float)fs->entryCount);
    if (layout.thumbBounds.height > layout.operationBounds.height)
        layout.thumbBounds.height = layout.operationBounds.height;
    layout.thumbBounds.x = layout.operationBounds.x + layout.operationBounds.width -
                           m->detailsScrollbarRightMargin - layout.thumbBounds.width;
    trackTravel = layout.operationBounds.height - layout.thumbBounds.height;
    normalizedScroll = layout.maxScroll > 0 ? (float)scroll / (float)layout.maxScroll : 0.0f;
    normalizedScroll = Clamp(normalizedScroll, 0.0f, 1.0f);
    layout.thumbBounds.y = layout.operationBounds.y + trackTravel * normalizedScroll;
    return layout;
}

static void ClampListScroll(RpgExplorerUiState *state, const DetailsScrollLayout *layout)
{
    if (state->listScroll < 0) state->listScroll = 0;
    if (state->listScroll > layout->maxScroll) state->listScroll = layout->maxScroll;
}

static int EntryAt(const ExplorerMetrics *m,const RpgExplorerFilesystem *fs,int scroll,
                   const DetailsScrollLayout *layout, Vector2 p)
{
    float contentTop = layout->rowViewport.y;
    float contentLeft = layout->rowViewport.x;
    float contentWidth = layout->rowViewport.width;
    int relativeRow;
    Rectangle row;
    if (!CheckCollisionPointRec(p, layout->rowViewport)) return -1;
    relativeRow = (int)((p.y - contentTop) / m->rowHeight);
    row = DetailsRowBounds(m, contentLeft, contentWidth, contentTop + relativeRow * m->rowHeight);
    if (!CheckCollisionPointRec(p, row)) return -1;
    relativeRow += scroll;
    return relativeRow >= 0 && relativeRow < fs->entryCount ? relativeRow : -1;
}
typedef struct NavigationRowLayout {
    Rectangle row;
    Rectangle chevronHitbox;
    Rectangle icon;
    float textX;
    float textY;
    bool expandable;
} NavigationRowLayout;

typedef enum NavigationHitKind {
    NAVIGATION_HIT_NONE,
    NAVIGATION_HIT_ROOT,
    NAVIGATION_HIT_NODE,
    NAVIGATION_HIT_CHEVRON
} NavigationHitKind;

typedef struct NavigationHit {
    NavigationHitKind kind;
    int nodeIndex;
} NavigationHit;

static Rectangle NavigationRowBounds(const ExplorerMetrics *m, float paneTop, int rowIndex)
{
    return (Rectangle){ 0.0f, paneTop + m->navTreeTopPadding + rowIndex * m->navRowHeight,
                        m->navigationPaneWidth, m->navRowHeight };
}

static bool TreeNodeHasChildren(const RpgExplorerFilesystem *fs, int nodeIndex)
{
    return fs != NULL && nodeIndex >= 0 && nodeIndex + 1 < fs->treeNodeCount &&
           fs->treeNodes[nodeIndex + 1].depth > fs->treeNodes[nodeIndex].depth;
}

static int BuildVisibleTreeNodes(const RpgExplorerUiState *state, const RpgExplorerFilesystem *fs,
                                 int *visibleNodes, int capacity)
{
    int count = 0;
    int hiddenAfterDepth = -1;
    if (state == NULL || fs == NULL || visibleNodes == NULL || capacity <= 0) return 0;
    for (int index = 0; index < fs->treeNodeCount; index++) {
        int depth = fs->treeNodes[index].depth;
        if (hiddenAfterDepth >= 0 && depth > hiddenAfterDepth) continue;
        if (hiddenAfterDepth >= 0 && depth <= hiddenAfterDepth) hiddenAfterDepth = -1;
        if (count >= capacity) break;
        visibleNodes[count++] = index;
        if (state->treeCollapsed[index] && TreeNodeHasChildren(fs, index)) hiddenAfterDepth = depth;
    }
    return count;
}

static NavigationRowLayout NavigationLayoutForRow(const ExplorerMetrics *m, float paneTop,
                                                    int rowIndex, int level, bool expandable)
{
    NavigationRowLayout layout;
    float levelOffset = level * m->navTreeLevelIndent;
    layout.row = NavigationRowBounds(m, paneTop, rowIndex);
    layout.chevronHitbox = (Rectangle){ m->navTreeChevronLeft + levelOffset, layout.row.y,
                                         m->navTreeChevronHitWidth, layout.row.height };
    layout.icon = (Rectangle){ m->navTreeIconLeft + levelOffset,
                               layout.row.y + (layout.row.height - m->fileIconSize) * 0.5f,
                               m->fileIconSize, m->fileIconSize };
    layout.textX = layout.icon.x + layout.icon.width + m->navTreeIconTextGap;
    layout.textY = layout.row.y + (layout.row.height - m->navigationTextSize) * 0.5f;
    layout.expandable = expandable;
    return layout;
}

static NavigationHit NavigationAt(const ExplorerMetrics *m, const RpgExplorerUiState *state,
                                  const RpgExplorerFilesystem *fs, Vector2 point)
{
    int visibleNodes[RPG_EXPLORER_MAX_TREE_NODES];
    int visibleCount;
    float paneTop = ContentTop(m);
    NavigationRowLayout root;
    NavigationHit hit = { NAVIGATION_HIT_NONE, -1 };
    if (point.x < 0.0f || point.x >= m->navigationPaneWidth || point.y < paneTop || point.y >= ContentBottom(m)) return hit;
    root = NavigationLayoutForRow(m, paneTop, 0, 0, false);
    if (CheckCollisionPointRec(point, root.row)) { hit.kind = NAVIGATION_HIT_ROOT; return hit; }
    visibleCount = BuildVisibleTreeNodes(state, fs, visibleNodes, RPG_EXPLORER_MAX_TREE_NODES);
    if (point.y >= root.row.y + root.row.height) {
        int row = (int)((point.y - (root.row.y + root.row.height)) / m->navRowHeight) + state->treeScroll;
        if (row >= 0 && row < visibleCount) {
            int nodeIndex = visibleNodes[row];
            NavigationRowLayout layout = NavigationLayoutForRow(m, paneTop, 1 + row - state->treeScroll,
                                                                 fs->treeNodes[nodeIndex].depth,
                                                                 TreeNodeHasChildren(fs, nodeIndex));
            hit.kind = layout.expandable && CheckCollisionPointRec(point, layout.chevronHitbox)
                           ? NAVIGATION_HIT_CHEVRON : NAVIGATION_HIT_NODE;
            hit.nodeIndex = nodeIndex;
        }
    }
    return hit;
}

static void DrawFolderIcon(const RpgExplorerShellCache *cache, Rectangle bounds)
{
    Texture2D icon = RpgExplorerShell_GetFolderTexture(cache);
    if (icon.id != 0) DrawTexturePro(icon, (Rectangle){ 0, 0, (float)icon.width, (float)icon.height }, bounds, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

static void DrawNavigationSelection(Rectangle row, const RpgExplorerTheme *theme, bool hovered)
{
    const ExplorerMetrics *m = &theme->metrics;
    float dpiScale = theme->dpiScale > 0.0f ? theme->dpiScale : 1.0f;
    float inset = m->navTreeSelectionHorizontalInset;
    float x0 = roundf(inset * dpiScale) / dpiScale;
    float x1 = roundf((m->navigationPaneWidth - inset) * dpiScale) / dpiScale;
    float y0 = roundf(row.y * dpiScale) / dpiScale;
    float y1 = roundf((row.y + row.height) * dpiScale) / dpiScale;
    float hairline = 1.0f / dpiScale;
    Rectangle bounds = { x0, y0, fmaxf(0.0f, x1 - x0), fmaxf(0.0f, y1 - y0) };
    DrawRectangleRec(bounds, hovered ? theme->navigationSelectionHover : theme->navigationSelection);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y, bounds.width, hairline }, theme->navigationSelectionBorder);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y + bounds.height - hairline, bounds.width, hairline }, theme->navigationSelectionBorder);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y, hairline, bounds.height }, theme->navigationSelectionBorder);
    DrawRectangleRec((Rectangle){ bounds.x + bounds.width - hairline, bounds.y, hairline, bounds.height }, theme->navigationSelectionBorder);
}

static void DrawNavigationLabel(const RpgExplorerTheme *theme, const char *text, float x, float y)
{
    char clipped[RPG_EXPLORER_NAME_LENGTH] = { 0 };
    size_t used = 0;
    float available = theme->metrics.navigationPaneWidth - theme->metrics.navTreeSelectionHorizontalInset - x;
    const char *cursor = text;
    while (cursor != NULL && *cursor != '\0' && used + 1U < sizeof(clipped)) {
        int bytes = 0;
        GetCodepointNext(cursor, &bytes);
        if (bytes <= 0 || used + (size_t)bytes >= sizeof(clipped)) break;
        memcpy(clipped + used, cursor, (size_t)bytes);
        clipped[used + (size_t)bytes] = '\0';
        if (RpgExplorerTheme_MeasureText(theme, clipped, theme->metrics.navigationTextSize).x > available) {
            clipped[used] = '\0';
            break;
        }
        used += (size_t)bytes;
        cursor += bytes;
    }
    RpgExplorerTheme_DrawText(theme, clipped, (Vector2){ x, y }, theme->metrics.navigationTextSize, theme->text);
}

static void Navigation(const RpgExplorerTheme *theme, const RpgExplorerFilesystem *fs,
                       const RpgExplorerShellCache *cache, const RpgExplorerUiState *state)
{
    const ExplorerMetrics *m = &theme->metrics;
    const float top = ContentTop(m), bottom = ContentBottom(m);
    const Vector2 mouse = GetMousePosition();
    int visibleNodes[RPG_EXPLORER_MAX_TREE_NODES];
    int visibleCount = BuildVisibleTreeNodes(state, fs, visibleNodes, RPG_EXPLORER_MAX_TREE_NODES);
    NavigationRowLayout root = NavigationLayoutForRow(m, top, 0, 0, false);
    bool rootSelected = _stricmp(fs->currentPath, fs->rootPath) == 0;
    bool rootHovered = CheckCollisionPointRec(mouse, root.row);
    int rowCapacity = (int)((bottom - (root.row.y + root.row.height)) / m->navRowHeight);
    DrawRectangle(0, (int)top, (int)m->navigationPaneWidth, (int)(bottom - top), theme->windowBackground);
    Line(m->navigationPaneWidth, top, m->navigationPaneWidth, bottom, theme);

    if (rootSelected) DrawNavigationSelection(root.row, theme, rootHovered);
    else if (rootHovered) DrawRectangleRec((Rectangle){ m->navTreeSelectionHorizontalInset, root.row.y,
                                                         m->navigationPaneWidth - m->navTreeSelectionHorizontalInset * 2.0f,
                                                         root.row.height }, theme->navigationHover);
    DrawFolderIcon(cache, root.icon);
    DrawNavigationLabel(theme, "Zipper", root.textX, root.textY);

    for (int visibleIndex = state->treeScroll; visibleIndex < visibleCount && visibleIndex < state->treeScroll + rowCapacity; visibleIndex++) {
        int nodeIndex = visibleNodes[visibleIndex];
        const RpgExplorerTreeNode *node = &fs->treeNodes[nodeIndex];
        int rowIndex = 1 + visibleIndex - state->treeScroll;
        NavigationRowLayout layout = NavigationLayoutForRow(m, top, rowIndex, node->depth, TreeNodeHasChildren(fs, nodeIndex));
        bool selected = _stricmp(node->path, fs->currentPath) == 0;
        bool hovered = CheckCollisionPointRec(mouse, layout.row);
        if (selected) DrawNavigationSelection(layout.row, theme, hovered);
        else if (hovered) DrawRectangleRec((Rectangle){ m->navTreeSelectionHorizontalInset, layout.row.y,
                                                         m->navigationPaneWidth - m->navTreeSelectionHorizontalInset * 2.0f,
                                                         layout.row.height }, theme->navigationHover);
        if (layout.expandable) {
            RpgExplorerTheme_DrawIconSized(theme, state->treeCollapsed[nodeIndex] ? ICON_CHEVRON_RIGHT : ICON_CHEVRON_DOWN,
                                           layout.chevronHitbox, m->navTreeChevronGlyphSize, theme->secondaryText);
        }
        DrawFolderIcon(cache, layout.icon);
        DrawNavigationLabel(theme, node->name, layout.textX, layout.textY);
    }
}

static void SizeText(uint64_t n,char *text,size_t size){if(n<1024)snprintf(text,size,"%llu B",(unsigned long long)n);else if(n<1024ULL*1024ULL)snprintf(text,size,"%.1f KB",(double)n/1024.0);else snprintf(text,size,"%.1f MB",(double)n/(1024.0*1024.0));}

typedef struct DetailsColumnLayout {
    float nameLeft;
    float dateLeft;
    float typeLeft;
    float sizeLeft;
    float tableRight;
    float scale;
} DetailsColumnLayout;

/* 通常幅では実測した固定列幅を使い、収まらない幅だけ均等に縮小する。 */
static DetailsColumnLayout GetDetailsColumnLayout(const ExplorerMetrics *m, float contentLeft, float contentWidth)
{
    float tableWidth = m->detailsNameColumnWidth + m->detailsDateColumnWidth +
                       m->detailsTypeColumnWidth + m->detailsSizeColumnWidth;
    float scale = contentWidth < tableWidth ? contentWidth / tableWidth : 1.0f;
    float nameWidth = m->detailsNameColumnWidth * scale;
    float dateWidth = m->detailsDateColumnWidth * scale;
    float typeWidth = m->detailsTypeColumnWidth * scale;
    float sizeWidth = m->detailsSizeColumnWidth * scale;
    DetailsColumnLayout layout = { contentLeft, 0.0f, 0.0f, 0.0f, 0.0f, scale };
    layout.dateLeft = layout.nameLeft + nameWidth;
    layout.typeLeft = layout.dateLeft + dateWidth;
    layout.sizeLeft = layout.typeLeft + typeWidth;
    layout.tableRight = layout.sizeLeft + sizeWidth;
    return layout;
}

/* raylib's HiDPI path takes the same logical coordinate space as the UI draw calls. */
static void BeginDetailsRowsClip(Rectangle logicalBounds, const RpgExplorerTheme *theme)
{
    (void)theme;
    BeginScissorMode((int)roundf(logicalBounds.x),
                     (int)roundf(logicalBounds.y),
                     (int)roundf(logicalBounds.width),
                     (int)roundf(logicalBounds.height));
}

static void DrawStatusHairline(float logicalY, const RpgExplorerTheme *theme)
{
    float dpiScale = theme->dpiScale > 0.0f ? theme->dpiScale : 1.0f;
    float y = roundf(logicalY * dpiScale) / dpiScale;
    DrawRectangleRec((Rectangle){ 0.0f, y, (float)GetScreenWidth(), 1.0f / dpiScale },
                     theme->separator);
}

static Rectangle StatusViewButtonBounds(const ExplorerMetrics *m, float statusTop, int index)
{
    float rightButtonX = (float)GetScreenWidth() - m->statusViewButtonSize;
    float x = index == 0 ? rightButtonX - m->statusViewButtonGap - m->statusViewButtonSize
                         : rightButtonX;
    return (Rectangle){ x, statusTop + (m->statusBarHeight - m->statusViewButtonSize) * 0.5f,
                        m->statusViewButtonSize, m->statusViewButtonSize };
}

static void DrawStatusSelectedViewButton(Rectangle bounds, int icon, const RpgExplorerTheme *theme)
{
    float dpiScale = theme->dpiScale > 0.0f ? theme->dpiScale : 1.0f;
    float hairline = 1.0f / dpiScale;
    bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    if (hovered) DrawRectangleRec(bounds, Fade(theme->accent, 0.04f));
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y, bounds.width, hairline }, theme->accent);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y + bounds.height - hairline, bounds.width, hairline }, theme->accent);
    DrawRectangleRec((Rectangle){ bounds.x, bounds.y, hairline, bounds.height }, theme->accent);
    DrawRectangleRec((Rectangle){ bounds.x + bounds.width - hairline, bounds.y, hairline, bounds.height }, theme->accent);
    RpgExplorerTheme_DrawIconSized(theme, icon, bounds, theme->metrics.statusViewGlyphSize, theme->accent);
}

static void DrawStatusBar(const RpgExplorerUiState *state, const RpgExplorerFilesystem *fs,
                          const RpgExplorerTheme *theme, float statusTop)
{
    const ExplorerMetrics *m = &theme->metrics;
    Rectangle detailsButton = StatusViewButtonBounds(m, statusTop, 0);
    Rectangle iconsButton = StatusViewButtonBounds(m, statusTop, 1);
    float textY = statusTop + (m->statusBarHeight - m->statusTextSize) * 0.5f;
    DrawStatusHairline(statusTop, theme);
    RpgExplorerTheme_DrawText(theme, TextFormat("%d 個の項目", fs->entryCount),
                              (Vector2){ m->statusTextLeftPadding, textY },
                              m->statusTextSize, theme->secondaryText);
    if (state->status[0] != '\0')
        RpgExplorerTheme_DrawText(theme, state->status,
                                  (Vector2){ m->navigationPaneWidth + m->statusTextLeftPadding, textY },
                                  m->statusTextSize, theme->accent);
    /* Details is the only implemented view. The Tiles control is intentionally disabled. */
    DrawStatusSelectedViewButton(detailsButton, ICON_VIEW, theme);
    RpgExplorerTheme_DrawIconSized(theme, ICON_TILES, iconsButton, m->statusViewGlyphSize,
                                   theme->disabledText);
}

static void Content(const RpgExplorerUiState *state,const RpgExplorerFilesystem *fs,const RpgExplorerShellCache *cache,const RpgExplorerTheme *theme)
{
    const ExplorerMetrics *m=&theme->metrics;float left=m->navigationPaneWidth,top=ContentTop(m),bottom=ContentBottom(m),width=GetScreenWidth()-left;DetailsColumnLayout columns=GetDetailsColumnLayout(m,left,width);DetailsScrollLayout scrollLayout=GetDetailsScrollLayout(m,fs,state->listScroll);float nameX=columns.nameLeft + m->detailsNameTextLeftPadding * columns.scale,dateX=columns.dateLeft + m->detailsColumnHeaderLeftPadding * columns.scale,typeX=columns.typeLeft + m->detailsColumnHeaderLeftPadding * columns.scale,sizeX=columns.sizeLeft + m->detailsColumnHeaderLeftPadding * columns.scale;
    float nameHeaderX=columns.nameLeft + m->detailsNameHeaderLeftPadding * columns.scale;
    DrawRectangle((int)left,(int)top,(int)width,(int)(bottom-top),theme->windowBackground);
    DetailsHeaderSeparator(columns.dateLeft, top, m->contentHeaderHeight, theme);
    DetailsHeaderSeparator(columns.typeLeft, top, m->contentHeaderHeight, theme);
    DetailsHeaderSeparator(columns.sizeLeft, top, m->contentHeaderHeight, theme);
    DetailsHeaderSeparator(columns.tableRight, top, m->contentHeaderHeight, theme);
    RpgExplorerTheme_DrawText(theme,"名前",(Vector2){nameHeaderX,top + m->detailsHeaderTextTopPadding},m->detailsTextSize,theme->secondaryText);RpgExplorerTheme_DrawText(theme,"更新日時",(Vector2){dateX,top + m->detailsHeaderTextTopPadding},m->detailsTextSize,theme->secondaryText);RpgExplorerTheme_DrawText(theme,"種類",(Vector2){typeX,top + m->detailsHeaderTextTopPadding},m->detailsTextSize,theme->secondaryText);RpgExplorerTheme_DrawText(theme,"サイズ",(Vector2){sizeX,top + m->detailsHeaderTextTopPadding},m->detailsTextSize,theme->secondaryText);Line(left,top+m->contentHeaderHeight,GetScreenWidth(),top+m->contentHeaderHeight,theme);
    BeginDetailsRowsClip(scrollLayout.rowViewport,theme);
    for(int v=0;v<scrollLayout.visibleRows;v++){int i=state->listScroll+v;if(i>=fs->entryCount)break;const RpgExplorerEntry *entry=&fs->entries[i];float y=scrollLayout.rowViewport.y+v*m->rowHeight;Rectangle row=DetailsRowBounds(m,left,scrollLayout.rowViewport.width,y);
        if(state->selectedEntry==i)DrawRectangleRounded(row,.12f,6,theme->selection);else if(CheckCollisionPointRec(GetMousePosition(),row))DrawRectangleRounded(row,.12f,6,theme->hover);
        Rectangle iconBounds={columns.nameLeft + m->detailsNameIconLeftPadding * columns.scale,row.y + (row.height-m->iconSize)*0.5f,m->iconSize,m->iconSize};float textY=y + m->detailsRowTextTopPadding;
        Texture2D icon=RpgExplorerShell_GetTexture(cache,entry->iconSlot);if(icon.id!=0)DrawTexturePro(icon,(Rectangle){0,0,(float)icon.width,(float)icon.height},iconBounds,(Vector2){0,0},0,state->isDragging&&state->draggedEntry==i?Fade(WHITE,.42f):WHITE);
        RpgExplorerTheme_DrawText(theme,entry->name,(Vector2){nameX,textY},m->detailsTextSize,state->isDragging&&state->draggedEntry==i?theme->disabledText:theme->text);char size[24],date[24];SizeText(entry->size,size,sizeof(size));RpgExplorerFilesystem_FormatDate(entry->lastWrite,date,sizeof(date));RpgExplorerTheme_DrawText(theme,date,(Vector2){dateX,textY},m->detailsTextSize,theme->secondaryText);RpgExplorerTheme_DrawText(theme,entry->typeName,(Vector2){typeX,textY},m->detailsTextSize,theme->secondaryText);if(!entry->isDirectory){float sizeTextWidth=RpgExplorerTheme_MeasureText(theme,size,m->detailsTextSize).x;float sizeTextX=columns.tableRight-m->detailsSizeRightPadding*columns.scale-sizeTextWidth;if(sizeTextX<columns.sizeLeft)sizeTextX=columns.sizeLeft;RpgExplorerTheme_DrawText(theme,size,(Vector2){sizeTextX,textY},m->detailsTextSize,theme->secondaryText);}}
    EndScissorMode();
    if(scrollLayout.hasOverflow){bool hovered=CheckCollisionPointRec(GetMousePosition(),scrollLayout.operationBounds);if(hovered||state->isListScrollbarDragging)DrawRectangleRec(scrollLayout.operationBounds,Fade(theme->secondaryText,.05f));DrawRectangleRounded(scrollLayout.thumbBounds,.5f,6,state->isListScrollbarDragging?theme->secondaryText:Fade(theme->secondaryText,hovered?.92f:.78f));}
    DrawStatusBar(state,fs,theme,bottom);
}

static void SetListScrollFromThumbY(RpgExplorerUiState *state, const DetailsScrollLayout *layout,
                                    float thumbY)
{
    float trackTravel = layout->operationBounds.height - layout->thumbBounds.height;
    float normalized;
    if (trackTravel <= 0.0f || layout->maxScroll <= 0) {
        state->listScroll = 0;
        return;
    }
    thumbY = Clamp(thumbY, layout->operationBounds.y,
                   layout->operationBounds.y + trackTravel);
    normalized = (thumbY - layout->operationBounds.y) / trackTravel;
    state->listScroll = (int)roundf(normalized * (float)layout->maxScroll);
    ClampListScroll(state, layout);
}

static void UpdateDetailsScrollbar(RpgExplorerUiState *state, const ExplorerMetrics *m,
                                   const RpgExplorerFilesystem *fs, Vector2 mouse)
{
    DetailsScrollLayout layout = GetDetailsScrollLayout(m, fs, state->listScroll);
    if (!layout.hasOverflow) {
        state->isListScrollbarDragging = false;
        state->listScrollbarGrabOffset = 0.0f;
        state->listScroll = 0;
        return;
    }

    ClampListScroll(state, &layout);
    layout = GetDetailsScrollLayout(m, fs, state->listScroll);
    if (state->isListScrollbarDragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            SetListScrollFromThumbY(state, &layout, mouse.y - state->listScrollbarGrabOffset);
        } else {
            /* Apply the release position as well, then let the next press start a new drag. */
            SetListScrollFromThumbY(state, &layout, mouse.y - state->listScrollbarGrabOffset);
            state->isListScrollbarDragging = false;
            state->listScrollbarGrabOffset = 0.0f;
        }
    } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
               CheckCollisionPointRec(mouse, layout.thumbBounds)) {
        state->isListScrollbarDragging = true;
        state->listScrollbarGrabOffset = mouse.y - layout.thumbBounds.y;
    }
}

void RpgExplorerUi_Initialize(RpgExplorerUiState *state){if(state==NULL)return;memset(state,0,sizeof(*state));state->selectedEntry=-1;state->draggedEntry=-1;state->lastClickEntry=-1;}

void RpgExplorerUi_UpdateAndDraw(RpgExplorerUiState *state,RpgExplorerFilesystem *fs,RpgExplorerShellCache *cache,RpgExplorerTheme *theme)
{
    RpgExplorerTheme_UpdateDpi(theme,GetWindowHandle());ExplorerMetrics *m=&theme->metrics;Vector2 p=GetMousePosition();float top=ContentTop(m);Rectangle up=NavigationButtonBounds(m,m->titleTabBarHeight,2),refresh=NavigationButtonBounds(m,m->titleTabBarHeight,3);
    RpgExplorerWindow_SetCaptionButtonWidth(m->captionButtonWidth);
    UpdateCaptionButtonLayout(theme);
    /* caption buttonはWM_NCHITTESTのnon-client経路だけで処理する。raylibは通常UIだけを扱う。 */
    if (!state->visibleGlyphsPrepared) EnsureVisibleGlyphs(state, fs, theme);
    if(IsKeyPressed(KEY_BACKSPACE)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(p,up))){if(RpgExplorerFilesystem_NavigateUp(fs)){state->listScroll=0;RefreshViewWithGlyphs(state,fs,cache,theme);}}if(IsKeyPressed(KEY_F5)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(p,refresh)))RefreshViewWithGlyphs(state,fs,cache,theme);
    {
        int visibleNodes[RPG_EXPLORER_MAX_TREE_NODES];
        int visibleTreeCount = BuildVisibleTreeNodes(state, fs, visibleNodes, RPG_EXPLORER_MAX_TREE_NODES);
        NavigationRowLayout root = NavigationLayoutForRow(m, top, 0, 0, false);
        float bottom = ContentBottom(m);
        DetailsScrollLayout scrollLayout = GetDetailsScrollLayout(m, fs, state->listScroll);
        int treeRows = (int)((bottom - (root.row.y + root.row.height)) / m->navRowHeight);
        if (p.x < m->navigationPaneWidth && p.y >= top && p.y < bottom) {
            state->treeScroll -= (int)GetMouseWheelMove();
            if (state->treeScroll < 0) state->treeScroll = 0;
            if (state->treeScroll > visibleTreeCount - treeRows) state->treeScroll = visibleTreeCount - treeRows;
            if (state->treeScroll < 0) state->treeScroll = 0;
        } else if (CheckCollisionPointRec(p, scrollLayout.rowViewport) ||
                   (scrollLayout.hasOverflow && CheckCollisionPointRec(p, scrollLayout.operationBounds))) {
            int wheel = (int)GetMouseWheelMove();
            if (wheel != 0) {
                state->listScroll -= wheel;
                ClampListScroll(state, &scrollLayout);
            }
        }
        UpdateDetailsScrollbar(state, m, fs, p);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        char breadcrumbPath[RPG_EXPLORER_PATH_LENGTH];
        Rectangle address = AddressBarBounds(m);
        if (BreadcrumbTargetAt(theme, fs, address, p, breadcrumbPath, (int)sizeof(breadcrumbPath))) {
            if (RpgExplorerFilesystem_NavigateTo(fs, breadcrumbPath)) {
                state->listScroll = 0;
                state->selectedEntry = -1;
                state->draggedEntry = -1;
                RefreshViewWithGlyphs(state, fs, cache, theme);
            }
        } else {
            NavigationHit tree = NavigationAt(m, state, fs, p);
            DetailsScrollLayout scrollLayout = GetDetailsScrollLayout(m, fs, state->listScroll);
            int entry = EntryAt(m, fs, state->listScroll, &scrollLayout, p);
            if (tree.kind == NAVIGATION_HIT_CHEVRON) {
                state->treeCollapsed[tree.nodeIndex] = !state->treeCollapsed[tree.nodeIndex];
                state->treeScroll = 0;
            } else if (tree.kind == NAVIGATION_HIT_ROOT) {
                if (RpgExplorerFilesystem_NavigateTo(fs, fs->rootPath)){state->listScroll=0;RefreshViewWithGlyphs(state, fs, cache, theme);}
            } else if (tree.kind == NAVIGATION_HIT_NODE) {
                if (RpgExplorerFilesystem_NavigateTo(fs, fs->treeNodes[tree.nodeIndex].path)){state->listScroll=0;RefreshViewWithGlyphs(state, fs, cache, theme);}
            } else if (entry >= 0) {
                bool dbl = state->lastClickEntry == entry && GetTime() - state->lastClickTime <= .35;
                state->selectedEntry = entry;
                state->draggedEntry = entry;
                state->dragStart = p;
                state->lastClickEntry = entry;
                state->lastClickTime = GetTime();
                if (dbl && RpgExplorerFilesystem_OpenEntry(fs, entry)) {
                    state->selectedEntry = -1;
                    state->draggedEntry = -1;
                    state->listScroll = 0;
                    RefreshViewWithGlyphs(state, fs, cache, theme);
                }
            }
        }
    }
    if(state->draggedEntry>=0&&IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&Vector2Distance(p,state->dragStart)>5.0f)state->isDragging=true;
    if (state->isDragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        const char *destination = NULL;
        NavigationHit tree = NavigationAt(m, state, fs, p);
        DetailsScrollLayout scrollLayout = GetDetailsScrollLayout(m, fs, state->listScroll);
        int entry = EntryAt(m, fs, state->listScroll, &scrollLayout, p);
        if (tree.kind == NAVIGATION_HIT_ROOT) destination = fs->rootPath;
        else if (tree.kind == NAVIGATION_HIT_NODE) destination = fs->treeNodes[tree.nodeIndex].path;
        else if (entry >= 0 && fs->entries[entry].isDirectory) destination = fs->entries[entry].path;
        if (destination != NULL && RpgExplorerFilesystem_MoveEntryToDirectory(fs, state->draggedEntry, destination)) {
            snprintf(state->status, sizeof(state->status), "移動しました");
            RefreshView(fs, cache);
        }
        state->isDragging = false;
        state->draggedEntry = -1;
    } else if (state->draggedEntry >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) state->draggedEntry = -1;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) state->visibleGlyphsPrepared = false;
    BeginDrawing();ClearBackground(theme->windowBackground);Chrome(theme,fs);Navigation(theme,fs,cache,state);Content(state,fs,cache,theme);if(state->isDragging&&state->draggedEntry>=0&&state->draggedEntry<fs->entryCount){DrawRectangleRounded((Rectangle){p.x + 12.0f,p.y + 12.0f,180.0f,28.0f},.16f,6,Fade(theme->accent,.88f));RpgExplorerTheme_DrawText(theme,fs->entries[state->draggedEntry].name,(Vector2){p.x + 21.0f,p.y + 18.0f},13.0f,RAYWHITE);}EndDrawing();
}
