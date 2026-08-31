// 役割: 本編ウィンドウの自動非表示タイトル操作領域を Win32 と raylib で橋渡しする。
#ifndef RPG_GAME_WINDOW_H
#define RPG_GAME_WINDOW_H

#include <stdbool.h>

/* raylib が作成した HWND を、ゲーム表示を妨げない client-area title bar として扱う。 */
bool RpgGameWindow_Install(void *nativeWindow, float titleHeight);
void RpgGameWindow_Uninstall(void);

/* マウスが上端へ近い間だけ、タイトル操作領域を有効・描画する。 */
void RpgGameWindow_UpdateAutoHide(void);
void RpgGameWindow_DrawChrome(void);

#endif
