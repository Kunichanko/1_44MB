// 依存する自プロジェクト内ファイル: なし
// 役割: 固定の論理解像度を可変サイズのウィンドウへレターボックス表示し、入力座標も論理座標へ統一する。
#ifndef RPG_VIEWPORT_H
#define RPG_VIEWPORT_H

#include "raylib.h"

enum { RPG_VIEWPORT_WIDTH = 960, RPG_VIEWPORT_HEIGHT = 540 };

// 本編だけが20x12のマス画面を使えるよう、初期化前に仮想表示サイズを切り替える。
bool RpgViewport_SetSize(int width, int height);
/* Rebuild the render texture without changing the native window. Used when the
   editor temporarily presents the exact same logical game viewport as Play. */
bool RpgViewport_Resize(int width, int height);
bool RpgViewport_Initialize(void);
void RpgViewport_Update(void);
void RpgViewport_BeginFrame(void);
void RpgViewport_EndFrame(void);
void RpgViewport_Shutdown(void);
int RpgViewport_GetWidth(void);
int RpgViewport_GetHeight(void);
Rectangle RpgViewport_GetContentBounds(void);
Texture2D RpgViewport_CopyLastFrameTexture(void);
// 表示先の物理ピクセルを、常に960×540基準のUI・ゲーム座標へ変換して返す。
Vector2 RpgViewport_GetMousePosition(void);

#endif
