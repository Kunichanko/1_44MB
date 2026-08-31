// 依存する自プロジェクト内ファイル: rpg_viewport.h
// 役割: 描画先テクスチャとマウス座標の変換を一元管理し、ウィンドウ拡大時の二重スケーリングを防ぐ。
#include "rpg_viewport.h"
#include "rpg_game_window.h"

#include <math.h>
#include <stddef.h>

// raylib と同名の Win32 API を読み込まないように最小限の型だけ使い、
// DPI 仮想化される raylib のマウス座標を経由しない。
static RenderTexture2D canvas;
static Rectangle contentBounds;
static float contentScale = 1.0f;
static int viewportWidth = RPG_VIEWPORT_WIDTH;
static int viewportHeight = RPG_VIEWPORT_HEIGHT;

bool RpgViewport_SetSize(int width, int height)
{
    // 描画先を作った後にサイズを変えると入力座標とテクスチャがずれるため、初期化前だけ受け付ける。
    if (canvas.id != 0 || width <= 0 || height <= 0) return false;
    viewportWidth = width;
    viewportHeight = height;
    return true;
}

bool RpgViewport_Resize(int width, int height)
{
    RenderTexture2D replacement;
    RenderTexture2D previous;
    if (width <= 0 || height <= 0) return false;
    if (width == viewportWidth && height == viewportHeight) return true;
    if (canvas.id == 0) return RpgViewport_SetSize(width, height);
    replacement = LoadRenderTexture(width, height);
    if (replacement.id == 0) return false;
    previous = canvas;
    canvas = replacement;
    viewportWidth = width;
    viewportHeight = height;
    UnloadRenderTexture(previous);
    RpgViewport_Update();
    return true;
}

bool RpgViewport_Initialize(void)
{
    canvas = LoadRenderTexture(viewportWidth, viewportHeight);
    contentBounds = (Rectangle){ 0.0f, 0.0f, (float)viewportWidth, (float)viewportHeight };
    RpgViewport_Update();
    return canvas.id != 0;
}

void RpgViewport_Update(void)
{
    int windowWidth = GetScreenWidth();
    int windowHeight = GetScreenHeight();
    if (windowWidth <= 0 || windowHeight <= 0) return;
    contentScale = fminf((float)windowWidth / viewportWidth,
                         (float)windowHeight / viewportHeight);
    contentBounds.width = viewportWidth * contentScale;
    contentBounds.height = viewportHeight * contentScale;
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
    /* 本編でだけ有効になる自動非表示のタイトル操作領域を、ゲーム映像の上に重ねる。 */
    RpgGameWindow_DrawChrome();
    EndDrawing();
}

void RpgViewport_Shutdown(void)
{
    if (canvas.id != 0) UnloadRenderTexture(canvas);
    canvas = (RenderTexture2D){ 0 };
}

int RpgViewport_GetWidth(void) { return viewportWidth; }
int RpgViewport_GetHeight(void) { return viewportHeight; }
Rectangle RpgViewport_GetContentBounds(void) { return contentBounds; }

Texture2D RpgViewport_CopyLastFrameTexture(void)
{
    Image image;
    Texture2D copy = { 0 };
    if (canvas.id == 0) return copy;
    image = LoadImageFromTexture(canvas.texture);
    if (image.data != NULL) {
        copy = LoadTextureFromImage(image);
        UnloadImage(image);
    }
    return copy;
}

Vector2 RpgViewport_GetMousePosition(void)
{
    // 描画に使うクライアント領域と同じWindows座標を取得する。
    // これによりモニターごとのDPI倍率がraylib入力へ先に適用されても判定がずれない。
    Vector2 pointer = GetMousePosition();
    return (Vector2){ (pointer.x - contentBounds.x) / contentScale,
                      (pointer.y - contentBounds.y) / contentScale };
}
