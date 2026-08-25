// 依存する自プロジェクト内ファイル: なし
// 役割: 固定の論理解像度を可変サイズのウィンドウへレターボックス表示し、入力座標も論理座標へ統一する。
#ifndef RPG_VIEWPORT_H
#define RPG_VIEWPORT_H

#include "raylib.h"

enum { RPG_VIEWPORT_WIDTH = 960, RPG_VIEWPORT_HEIGHT = 540 };

bool RpgViewport_Initialize(void);
void RpgViewport_Update(void);
void RpgViewport_BeginFrame(void);
void RpgViewport_EndFrame(void);
void RpgViewport_Shutdown(void);
int RpgViewport_GetWidth(void);
int RpgViewport_GetHeight(void);
Rectangle RpgViewport_GetContentBounds(void);
// 表示先の物理ピクセルを、常に960×540基準のUI・ゲーム座標へ変換して返す。
Vector2 RpgViewport_GetMousePosition(void);

#endif
