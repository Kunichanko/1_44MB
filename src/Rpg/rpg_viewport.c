// 依存する自プロジェクト内ファイル: rpg_viewport.h
// 役割: 描画先テクスチャとマウス座標の変換を一元管理し、ウィンドウ拡大時の二重スケーリングを防ぐ。
#include "rpg_viewport.h"

#include <math.h>

// raylib と同名の Win32 API を読み込まないように最小限の型だけ使い、
// DPI 仮想化される raylib のマウス座標を経由しない。
static RenderTexture2D canvas;
static Rectangle contentBounds;
static float contentScale = 1.0f;

bool RpgViewport_Initialize(void)
{
    canvas = LoadRenderTexture(RPG_VIEWPORT_WIDTH, RPG_VIEWPORT_HEIGHT);
    contentBounds = (Rectangle){ 0.0f, 0.0f, RPG_VIEWPORT_WIDTH, RPG_VIEWPORT_HEIGHT };
    RpgViewport_Update();
    return canvas.id != 0;
}

void RpgViewport_Update(void)
{
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();
    if (windowWidth <= 0 || windowHeight <= 0) return;
    contentScale = fminf((float)windowWidth / RPG_VIEWPORT_WIDTH,
                         (float)windowHeight / RPG_VIEWPORT_HEIGHT);
    contentBounds.width = RPG_VIEWPORT_WIDTH * contentScale;
    contentBounds.height = RPG_VIEWPORT_HEIGHT * contentScale;
    contentBounds.x = floorf(((float)windowWidth - contentBounds.width) * 0.5f);
    contentBounds.y = floorf(((float)windowHeight - contentBounds.height) * 0.5f);
    // 入力変換は RpgViewport_GetMousePosition() だけで行う。
    // raylibの内部倍率設定を併用すると、OS/DPI設定次第で二重変換になるため使用しない。
}

void RpgViewport_BeginFrame(void) { BeginTextureMode(canvas); }

void RpgViewport_EndFrame(void)
{
    EndTextureMode();
    BeginDrawing();
    ClearBackground((Color){ 28, 28, 28, 255 });
    DrawTexturePro(canvas.texture,
                   (Rectangle){ 0.0f, 0.0f, (float)canvas.texture.width,
                                -(float)canvas.texture.height },
                   contentBounds, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndDrawing();
}

void RpgViewport_Shutdown(void)
{
    if (canvas.id != 0) UnloadRenderTexture(canvas);
    canvas = (RenderTexture2D){ 0 };
}

int RpgViewport_GetWidth(void) { return RPG_VIEWPORT_WIDTH; }
int RpgViewport_GetHeight(void) { return RPG_VIEWPORT_HEIGHT; }
Rectangle RpgViewport_GetContentBounds(void) { return contentBounds; }

Vector2 RpgViewport_GetMousePosition(void)
{
    // 描画に使うクライアント領域と同じWindows座標を取得する。
    // これによりモニターごとのDPI倍率がraylib入力へ先に適用されても判定がずれない。
    Vector2 pointer = GetMousePosition();
    return (Vector2){ (pointer.x - contentBounds.x) / contentScale,
                      (pointer.y - contentBounds.y) / contentScale };
}
