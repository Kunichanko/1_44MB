// 依存する自プロジェクト内ファイル: rpg_explorer_ui.h。
// 役割: ExplorerMetrics と Windows 標準素材を使い、Zipper 専用の Windows 11 Explorer 風 UI を描画・操作する。
#include "rpg_explorer_ui.h"

#include <stdio.h>
#include <string.h>

#include "raymath.h"

enum { ICON_NEW = 0xE710, ICON_BACK = 0xE72B, ICON_FORWARD = 0xE72A, ICON_UP = 0xE74A,
       ICON_REFRESH = 0xE72C, ICON_SEARCH = 0xE721, ICON_CUT = 0xE8C6, ICON_COPY = 0xE8C8,
       ICON_PASTE = 0xE77F, ICON_RENAME = 0xE8AC, ICON_SHARE = 0xE72D, ICON_DELETE = 0xE74D,
       ICON_SORT = 0xE8CB, ICON_VIEW = 0xE8A9, ICON_MORE = 0xE712 };

static float ContentTop(const ExplorerMetrics *m) { return m->titleTabBarHeight + m->navigationAddressBarHeight + m->commandBarHeight; }
static void RefreshView(RpgExplorerFilesystem *fs, RpgExplorerShellCache *cache) { if (RpgExplorerFilesystem_Refresh(fs)) RpgExplorerShell_ResolveEntries(cache, fs); }
static void Line(float x1, float y1, float x2, float y2, const RpgExplorerTheme *theme) { DrawLineEx((Vector2){x1,y1}, (Vector2){x2,y2}, 1.0f, theme->separator); }

static void IconButton(const RpgExplorerTheme *theme, Rectangle bounds, int icon, bool enabled)
{
    if (enabled && CheckCollisionPointRec(GetMousePosition(), bounds)) DrawRectangleRounded(bounds, 0.16f, 6, theme->hover);
    RpgExplorerTheme_DrawIcon(theme, icon, bounds, enabled ? theme->text : theme->disabledText);
}

static void LabelButton(const RpgExplorerTheme *theme, Rectangle bounds, int icon, const char *label, bool enabled)
{
    const ExplorerMetrics *m = &theme->metrics;
    if (enabled && CheckCollisionPointRec(GetMousePosition(), bounds)) DrawRectangleRounded(bounds, 0.16f, 6, theme->hover);
    RpgExplorerTheme_DrawIcon(theme, icon, (Rectangle){ bounds.x + 6.0f*m->dpiScale, bounds.y, m->commandButtonHeight, bounds.height }, enabled ? theme->text : theme->disabledText);
    RpgExplorerTheme_DrawText(theme, label, (Vector2){ bounds.x + m->commandButtonHeight + 6.0f*m->dpiScale, bounds.y + 9.0f*m->dpiScale }, 14.0f*m->dpiScale, enabled ? theme->text : theme->disabledText);
}

static void Chevron(Vector2 p, bool right, Color color, float s)
{
    if (right) { DrawLineEx(p,(Vector2){p.x+5*s,p.y+5*s},1.3f*s,color); DrawLineEx((Vector2){p.x,p.y+10*s},(Vector2){p.x+5*s,p.y+5*s},1.3f*s,color); }
    else { DrawLineEx((Vector2){p.x+5*s,p.y},(Vector2){p.x,p.y+5*s},1.3f*s,color); DrawLineEx((Vector2){p.x+5*s,p.y+10*s},(Vector2){p.x,p.y+5*s},1.3f*s,color); }
}

static void AddressBar(const RpgExplorerTheme *theme, const RpgExplorerFilesystem *fs, Rectangle bounds)
{
    const ExplorerMetrics *m = &theme->metrics;
    const char *relative = RpgExplorerFilesystem_GetRelativePath(fs, fs->currentPath);
    float cursor = bounds.x + 14.0f*m->dpiScale;
    DrawRectangleRounded(bounds, 0.18f, 8, Fade(theme->chromeBackground, 0.55f));
    DrawRectangleLinesEx(bounds, 1.0f, theme->separator);
    RpgExplorerTheme_DrawIcon(theme, ICON_VIEW, (Rectangle){cursor,bounds.y+6*m->dpiScale,20*m->dpiScale,20*m->dpiScale}, theme->secondaryText);
    cursor += 28.0f*m->dpiScale;
    RpgExplorerTheme_DrawText(theme,"Zipper",(Vector2){cursor,bounds.y+7*m->dpiScale},15*m->dpiScale,theme->text);
    cursor += RpgExplorerTheme_MeasureText(theme,"Zipper",15*m->dpiScale).x + 13*m->dpiScale;
    char segments[RPG_EXPLORER_PATH_LENGTH];
    snprintf(segments,sizeof(segments),"%s",relative);
    for (char *part = strtok(segments,"\\/"); part != NULL; part = strtok(NULL,"\\/")) {
        Chevron((Vector2){cursor,bounds.y+11*m->dpiScale},true,theme->secondaryText,m->dpiScale); cursor += 14*m->dpiScale;
        RpgExplorerTheme_DrawText(theme,part,(Vector2){cursor,bounds.y+7*m->dpiScale},15*m->dpiScale,theme->text);
        cursor += RpgExplorerTheme_MeasureText(theme,part,15*m->dpiScale).x + 13*m->dpiScale;
    }
}

static void Chrome(const RpgExplorerTheme *theme, const RpgExplorerFilesystem *fs)
{
    const ExplorerMetrics *m = &theme->metrics;
    float width = (float)GetScreenWidth(), tabHeight = m->titleTabBarHeight;
    Rectangle tab = {8*m->dpiScale,4*m->dpiScale,276*m->dpiScale,tabHeight-4*m->dpiScale};
    DrawRectangle(0,0,GetScreenWidth(),(int)tabHeight,theme->chromeBackground);
    DrawRectangleRounded(tab,0.18f,8,theme->windowBackground);
    RpgExplorerTheme_DrawIcon(theme,ICON_VIEW,(Rectangle){tab.x+14*m->dpiScale,tab.y+10*m->dpiScale,18*m->dpiScale,18*m->dpiScale},theme->accent);
    RpgExplorerTheme_DrawText(theme,"Zipper",(Vector2){tab.x+43*m->dpiScale,tab.y+12*m->dpiScale},16*m->dpiScale,theme->text);
    RpgExplorerTheme_DrawText(theme,"+",(Vector2){tab.x+tab.width+24*m->dpiScale,tab.y+9*m->dpiScale},24*m->dpiScale,theme->secondaryText);
    Line(0,tabHeight,width,tabHeight,theme);

    float navTop=tabHeight;
    DrawRectangle(0,(int)navTop,GetScreenWidth(),(int)m->navigationAddressBarHeight,theme->windowBackground);
    Rectangle back={12*m->dpiScale,navTop+6*m->dpiScale,m->commandButtonWidth,m->commandButtonHeight};
    Rectangle forward={back.x+m->commandButtonWidth,back.y,m->commandButtonWidth,m->commandButtonHeight};
    Rectangle up={forward.x+m->commandButtonWidth,back.y,m->commandButtonWidth,m->commandButtonHeight};
    Rectangle refresh={up.x+m->commandButtonWidth,back.y,m->commandButtonWidth,m->commandButtonHeight};
    IconButton(theme,back,ICON_BACK,true); IconButton(theme,forward,ICON_FORWARD,false); IconButton(theme,up,ICON_UP,_stricmp(fs->currentPath,fs->rootPath)!=0); IconButton(theme,refresh,ICON_REFRESH,true);
    float searchX=width-m->searchWidth-m->horizontalPadding;
    Rectangle address={refresh.x+m->commandButtonWidth+8*m->dpiScale,navTop+8*m->dpiScale,searchX-refresh.x-m->commandButtonWidth-18*m->dpiScale,m->addressHeight};
    AddressBar(theme,fs,address);
    Rectangle search={searchX,address.y,m->searchWidth,m->addressHeight};
    DrawRectangleRounded(search,0.18f,8,Fade(theme->chromeBackground,0.64f));
    DrawRectangleLinesEx(search,1.0f,theme->separator);
    RpgExplorerTheme_DrawText(theme,"Zipper の検索",(Vector2){search.x+12*m->dpiScale,search.y+7*m->dpiScale},15*m->dpiScale,theme->secondaryText);
    RpgExplorerTheme_DrawIcon(theme,ICON_SEARCH,(Rectangle){search.x+search.width-30*m->dpiScale,search.y+6*m->dpiScale,20*m->dpiScale,20*m->dpiScale},theme->text);
    Line(0,navTop+m->navigationAddressBarHeight,width,navTop+m->navigationAddressBarHeight,theme);

    float cmdTop=tabHeight+m->navigationAddressBarHeight,cursor=m->horizontalPadding;
    DrawRectangle(0,(int)cmdTop,GetScreenWidth(),(int)m->commandBarHeight,theme->windowBackground);
    LabelButton(theme,(Rectangle){cursor,cmdTop+6*m->dpiScale,102*m->dpiScale,m->commandButtonHeight},ICON_NEW,"新規作成",false); cursor+=110*m->dpiScale;
    const int icons[]={ICON_CUT,ICON_COPY,ICON_PASTE,ICON_RENAME,ICON_SHARE,ICON_DELETE};
    for(int i=0;i<6;i++){IconButton(theme,(Rectangle){cursor,cmdTop+6*m->dpiScale,m->commandButtonWidth,m->commandButtonHeight},icons[i],false);cursor+=m->commandButtonWidth;}
    Line(cursor+8*m->dpiScale,cmdTop+7*m->dpiScale,cursor+8*m->dpiScale,cmdTop+m->commandBarHeight-7*m->dpiScale,theme); cursor+=20*m->dpiScale;
    LabelButton(theme,(Rectangle){cursor,cmdTop+6*m->dpiScale,100*m->dpiScale,m->commandButtonHeight},ICON_SORT,"並べ替え",true);cursor+=104*m->dpiScale;
    LabelButton(theme,(Rectangle){cursor,cmdTop+6*m->dpiScale,88*m->dpiScale,m->commandButtonHeight},ICON_VIEW,"表示",true);cursor+=94*m->dpiScale;
    IconButton(theme,(Rectangle){cursor,cmdTop+6*m->dpiScale,m->commandButtonWidth,m->commandButtonHeight},ICON_MORE,true);
    Line(0,cmdTop+m->commandBarHeight,width,cmdTop+m->commandBarHeight,theme);
}

static int EntryAt(const ExplorerMetrics *m,const RpgExplorerFilesystem *fs,int scroll,Vector2 p)
{ float top=ContentTop(m)+m->contentHeaderHeight; if(p.x<m->navigationPaneWidth||p.y<top)return -1; int i=scroll+(int)((p.y-top)/m->rowHeight); return i>=0&&i<fs->entryCount?i:-1; }
static int TreeAt(const ExplorerMetrics *m,const RpgExplorerFilesystem *fs,int scroll,Vector2 p)
{ float top=ContentTop(m); if(p.x>=m->navigationPaneWidth||p.y<top)return -2; if(p.y<top+34*m->dpiScale)return -1; int i=scroll+(int)((p.y-top-34*m->dpiScale)/(28*m->dpiScale)); return i>=0&&i<fs->treeNodeCount?i:-2; }

static void DrawFolderIcon(const RpgExplorerShellCache *cache, Rectangle bounds)
{
    Texture2D icon = RpgExplorerShell_GetFolderTexture(cache);
    if (icon.id != 0) DrawTexturePro(icon, (Rectangle){ 0, 0, (float)icon.width, (float)icon.height }, bounds, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

static void Navigation(const RpgExplorerTheme *theme,const RpgExplorerFilesystem *fs,const RpgExplorerShellCache *cache,int scroll)
{
    const ExplorerMetrics *m=&theme->metrics; float top=ContentTop(m),row=28*m->dpiScale;
    DrawRectangle(0,(int)top,(int)m->navigationPaneWidth,GetScreenHeight()-(int)top,theme->windowBackground);Line(m->navigationPaneWidth,top,m->navigationPaneWidth,(float)GetScreenHeight(),theme);
    if(_stricmp(fs->currentPath,fs->rootPath)==0)DrawRectangleRounded((Rectangle){8*m->dpiScale,top+5*m->dpiScale,m->navigationPaneWidth-16*m->dpiScale,row},0.16f,6,theme->selection);
    Chevron((Vector2){18*m->dpiScale,top+14*m->dpiScale},false,theme->text,m->dpiScale);
    DrawFolderIcon(cache, (Rectangle){34*m->dpiScale,top+6*m->dpiScale,m->fileIconSize,m->fileIconSize});
    RpgExplorerTheme_DrawText(theme,"Zipper",(Vector2){62*m->dpiScale,top+10*m->dpiScale},15*m->dpiScale,theme->text);
    int visible=(GetScreenHeight()-(int)(top+36*m->dpiScale))/(int)row;
    for(int v=0;v<visible;v++){int i=scroll+v;if(i>=fs->treeNodeCount)break;const RpgExplorerTreeNode *node=&fs->treeNodes[i];float y=top+36*m->dpiScale+v*row,indent=node->depth*14*m->dpiScale;
        if(_stricmp(node->path,fs->currentPath)==0)DrawRectangleRounded((Rectangle){8*m->dpiScale,y,m->navigationPaneWidth-16*m->dpiScale,row},0.16f,6,theme->selection);
        Chevron((Vector2){18*m->dpiScale+indent,y+9*m->dpiScale},true,theme->secondaryText,m->dpiScale);
        DrawFolderIcon(cache, (Rectangle){34*m->dpiScale+indent,y+5*m->dpiScale,m->fileIconSize,m->fileIconSize});
        RpgExplorerTheme_DrawText(theme,node->name,(Vector2){61*m->dpiScale+indent,y+6*m->dpiScale},14*m->dpiScale,theme->text);}
}

static void SizeText(uint64_t n,char *text,size_t size){if(n<1024)snprintf(text,size,"%llu B",(unsigned long long)n);else if(n<1024ULL*1024ULL)snprintf(text,size,"%.1f KB",(double)n/1024.0);else snprintf(text,size,"%.1f MB",(double)n/(1024.0*1024.0));}

static void Content(const RpgExplorerUiState *state,const RpgExplorerFilesystem *fs,const RpgExplorerShellCache *cache,const RpgExplorerTheme *theme)
{
    const ExplorerMetrics *m=&theme->metrics;float left=m->navigationPaneWidth,top=ContentTop(m),width=GetScreenWidth()-left,nameX=left+48*m->dpiScale,dateX=left+width*.40f,typeX=left+width*.64f,sizeX=left+width*.84f;
    DrawRectangle((int)left,(int)top,(int)width,GetScreenHeight()-(int)top,theme->windowBackground);
    RpgExplorerTheme_DrawText(theme,"名前",(Vector2){nameX,top+8*m->dpiScale},14*m->dpiScale,theme->secondaryText);RpgExplorerTheme_DrawText(theme,"更新日時",(Vector2){dateX,top+8*m->dpiScale},14*m->dpiScale,theme->secondaryText);RpgExplorerTheme_DrawText(theme,"種類",(Vector2){typeX,top+8*m->dpiScale},14*m->dpiScale,theme->secondaryText);RpgExplorerTheme_DrawText(theme,"サイズ",(Vector2){sizeX,top+8*m->dpiScale},14*m->dpiScale,theme->secondaryText);Line(left,top+m->contentHeaderHeight,GetScreenWidth(),top+m->contentHeaderHeight,theme);
    int visible=(GetScreenHeight()-(int)(top+m->contentHeaderHeight))/(int)m->rowHeight;
    for(int v=0;v<visible;v++){int i=state->listScroll+v;if(i>=fs->entryCount)break;const RpgExplorerEntry *entry=&fs->entries[i];float y=top+m->contentHeaderHeight+v*m->rowHeight;Rectangle row={left+6*m->dpiScale,y+2*m->dpiScale,width-12*m->dpiScale,m->rowHeight-4*m->dpiScale};
        if(state->selectedEntry==i)DrawRectangleRounded(row,.12f,6,theme->selection);else if(CheckCollisionPointRec(GetMousePosition(),row))DrawRectangleRounded(row,.12f,6,theme->hover);
        Texture2D icon=RpgExplorerShell_GetTexture(cache,entry->iconSlot);if(icon.id!=0)DrawTexturePro(icon,(Rectangle){0,0,(float)icon.width,(float)icon.height},(Rectangle){left+21*m->dpiScale,y+7*m->dpiScale,m->iconSize,m->iconSize},(Vector2){0,0},0,state->isDragging&&state->draggedEntry==i?Fade(WHITE,.42f):WHITE);
        RpgExplorerTheme_DrawText(theme,entry->name,(Vector2){nameX,y+7*m->dpiScale},14*m->dpiScale,state->isDragging&&state->draggedEntry==i?theme->disabledText:theme->text);char size[24],date[24];SizeText(entry->size,size,sizeof(size));RpgExplorerFilesystem_FormatDate(entry->lastWrite,date,sizeof(date));RpgExplorerTheme_DrawText(theme,date,(Vector2){dateX,y+7*m->dpiScale},14*m->dpiScale,theme->secondaryText);RpgExplorerTheme_DrawText(theme,entry->typeName,(Vector2){typeX,y+7*m->dpiScale},14*m->dpiScale,theme->secondaryText);if(!entry->isDirectory)RpgExplorerTheme_DrawText(theme,size,(Vector2){sizeX,y+7*m->dpiScale},14*m->dpiScale,theme->secondaryText);}
    RpgExplorerTheme_DrawText(theme,TextFormat("%d 個の項目",fs->entryCount),(Vector2){14*m->dpiScale,GetScreenHeight()-23*m->dpiScale},13*m->dpiScale,theme->secondaryText);if(state->status[0]!='\0')RpgExplorerTheme_DrawText(theme,state->status,(Vector2){left+14*m->dpiScale,GetScreenHeight()-23*m->dpiScale},13*m->dpiScale,theme->accent);
}

void RpgExplorerUi_Initialize(RpgExplorerUiState *state){if(state==NULL)return;memset(state,0,sizeof(*state));state->selectedEntry=-1;state->draggedEntry=-1;state->lastClickEntry=-1;}

void RpgExplorerUi_UpdateAndDraw(RpgExplorerUiState *state,RpgExplorerFilesystem *fs,RpgExplorerShellCache *cache,RpgExplorerTheme *theme)
{
    RpgExplorerTheme_UpdateDpi(theme,GetWindowHandle());ExplorerMetrics *m=&theme->metrics;Vector2 p=GetMousePosition();float top=ContentTop(m);Rectangle up={12*m->dpiScale+2*m->commandButtonWidth,m->titleTabBarHeight+6*m->dpiScale,m->commandButtonWidth,m->commandButtonHeight},refresh={up.x+m->commandButtonWidth,up.y,m->commandButtonWidth,m->commandButtonHeight};
    if(IsKeyPressed(KEY_BACKSPACE)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(p,up))){if(RpgExplorerFilesystem_NavigateUp(fs))RefreshView(fs,cache);}if(IsKeyPressed(KEY_F5)||(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(p,refresh)))RefreshView(fs,cache);
    int listRows=(GetScreenHeight()-(int)(top+m->contentHeaderHeight))/(int)m->rowHeight,treeRows=(GetScreenHeight()-(int)(top+36*m->dpiScale))/(int)(28*m->dpiScale);if(p.x<m->navigationPaneWidth&&p.y>=top){state->treeScroll-=(int)GetMouseWheelMove();if(state->treeScroll<0)state->treeScroll=0;if(state->treeScroll>fs->treeNodeCount-treeRows)state->treeScroll=fs->treeNodeCount-treeRows;if(state->treeScroll<0)state->treeScroll=0;}else if(p.x>=m->navigationPaneWidth&&p.y>=top){state->listScroll-=(int)GetMouseWheelMove();if(state->listScroll<0)state->listScroll=0;if(state->listScroll>fs->entryCount-listRows)state->listScroll=fs->entryCount-listRows;if(state->listScroll<0)state->listScroll=0;}
    if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){int tree=TreeAt(m,fs,state->treeScroll,p),entry=EntryAt(m,fs,state->listScroll,p);if(tree==-1){if(RpgExplorerFilesystem_NavigateTo(fs,fs->rootPath))RefreshView(fs,cache);}else if(tree>=0){if(RpgExplorerFilesystem_NavigateTo(fs,fs->treeNodes[tree].path))RefreshView(fs,cache);}else if(entry>=0){bool dbl=state->lastClickEntry==entry&&GetTime()-state->lastClickTime<=.35;state->selectedEntry=entry;state->draggedEntry=entry;state->dragStart=p;state->lastClickEntry=entry;state->lastClickTime=GetTime();if(dbl&&RpgExplorerFilesystem_OpenEntry(fs,entry)){state->selectedEntry=-1;state->draggedEntry=-1;RefreshView(fs,cache);}}}
    if(state->draggedEntry>=0&&IsMouseButtonDown(MOUSE_BUTTON_LEFT)&&Vector2Distance(p,state->dragStart)>5*m->dpiScale)state->isDragging=true;
    if(state->isDragging&&IsMouseButtonReleased(MOUSE_BUTTON_LEFT)){const char *destination=NULL;int tree=TreeAt(m,fs,state->treeScroll,p),entry=EntryAt(m,fs,state->listScroll,p);if(tree==-1)destination=fs->rootPath;else if(tree>=0)destination=fs->treeNodes[tree].path;else if(entry>=0&&fs->entries[entry].isDirectory)destination=fs->entries[entry].path;if(destination!=NULL&&RpgExplorerFilesystem_MoveEntryToDirectory(fs,state->draggedEntry,destination)){snprintf(state->status,sizeof(state->status),"移動しました");RefreshView(fs,cache);}state->isDragging=false;state->draggedEntry=-1;}else if(state->draggedEntry>=0&&IsMouseButtonReleased(MOUSE_BUTTON_LEFT))state->draggedEntry=-1;
    BeginDrawing();ClearBackground(theme->windowBackground);Chrome(theme,fs);Navigation(theme,fs,cache,state->treeScroll);Content(state,fs,cache,theme);if(state->isDragging&&state->draggedEntry>=0&&state->draggedEntry<fs->entryCount){DrawRectangleRounded((Rectangle){p.x+12*m->dpiScale,p.y+12*m->dpiScale,180*m->dpiScale,28*m->dpiScale},.16f,6,Fade(theme->accent,.88f));RpgExplorerTheme_DrawText(theme,fs->entries[state->draggedEntry].name,(Vector2){p.x+21*m->dpiScale,p.y+18*m->dpiScale},13*m->dpiScale,RAYWHITE);}EndDrawing();
}
