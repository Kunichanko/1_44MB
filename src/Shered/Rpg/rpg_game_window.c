// 役割: 本編用の client-area title bar、Win32 のリサイズ・ドラッグ・caption 操作を実装する。
#include "rpg_game_window.h"

#ifdef _WIN32
#include <math.h>

#define WIN32_LEAN_AND_MEAN
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#define LoadImage Win32LoadImage
#include <windows.h>
#include <windowsx.h>
#undef LoadImage
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle

#include "raylib.h"

extern UINT WINAPI GetDpiForWindow(HWND window);
extern int WINAPI GetSystemMetricsForDpi(int index, UINT dpi);

typedef enum RpgGameCaptionButton {
    RPG_GAME_CAPTION_MINIMIZE,
    RPG_GAME_CAPTION_MAXIMIZE,
    RPG_GAME_CAPTION_CLOSE
} RpgGameCaptionButton;

static HWND gameWindow;
static WNDPROC originalWindowProcedure;
static float gameTitleHeight = 40.0f;
static bool chromeVisible;
static char gameWindowTitle[128] = "1_44MB - RPG Version";
static Rectangle captionBounds[3];
static int pressedCaptionButton = -1;

static bool IsInside(Rectangle bounds, float x, float y)
{
    return x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width && y < bounds.y + bounds.height;
}

static void UpdateCaptionBounds(void)
{
    RECT client;
    float width;
    const float buttonWidth = 42.0f;
    if (gameWindow == NULL || !chromeVisible || !GetClientRect(gameWindow, &client)) {
        for (int index = 0; index < 3; index++) captionBounds[index] = (Rectangle){ 0 };
        return;
    }
    /* raylib's screen size is the coordinate space DrawChrome uses.  Do not
       derive this from a Win32 DPI value: a non-HiDPI raylib window may have
       a different client-pixel-to-raylib ratio. */
    width = (float)GetScreenWidth();
    captionBounds[RPG_GAME_CAPTION_MINIMIZE] = (Rectangle){ width - buttonWidth * 3.0f, 0.0f, buttonWidth, gameTitleHeight };
    captionBounds[RPG_GAME_CAPTION_MAXIMIZE] = (Rectangle){ width - buttonWidth * 2.0f, 0.0f, buttonWidth, gameTitleHeight };
    captionBounds[RPG_GAME_CAPTION_CLOSE] = (Rectangle){ width - buttonWidth, 0.0f, buttonWidth, gameTitleHeight };
}

/* Win32 client points are physical pixels. Convert them to the same raylib
   logical coordinate system that owns captionBounds. */
static bool ClientPointToRaylibLogical(HWND window, POINT point, float *x, float *y)
{
    RECT client;
    int width, height;
    if (window == NULL || x == NULL || y == NULL || !GetClientRect(window, &client)) return false;
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || GetScreenWidth() <= 0 || GetScreenHeight() <= 0) return false;
    *x = (float)point.x * (float)GetScreenWidth() / (float)width;
    *y = (float)point.y * (float)GetScreenHeight() / (float)height;
    return true;
}

static bool GetCursorLogicalPosition(float *x, float *y)
{
    POINT point;
    if (gameWindow == NULL || x == NULL || y == NULL || !GetCursorPos(&point) ||
        !ScreenToClient(gameWindow, &point)) return false;
    return ClientPointToRaylibLogical(gameWindow, point, x, y);
}

static int GetCaptionButtonAt(float x, float y)
{
    for (int index = 0; index < 3; index++)
        if (IsInside(captionBounds[index], x, y)) return index;
    return -1;
}

static LRESULT HitTestResizeBorder(HWND window, POINT point)
{
    RECT frame;
    UINT dpi;
    int border;
    bool left, right, top, bottom;
    if (IsZoomed(window) || (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME) == 0 ||
        !GetWindowRect(window, &frame)) return HTNOWHERE;
    dpi = GetDpiForWindow(window);
    border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    left = point.x < frame.left + border;
    right = point.x >= frame.right - border;
    top = point.y < frame.top + border;
    bottom = point.y >= frame.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTNOWHERE;
}

static void FinishCaptionPress(HWND window)
{
    int button = pressedCaptionButton;
    float x, y;
    pressedCaptionButton = -1;
    if (GetCapture() == window) ReleaseCapture();
    if (button < 0 || !GetCursorLogicalPosition(&x, &y) || GetCaptionButtonAt(x, y) != button) return;
    switch ((RpgGameCaptionButton)button) {
    case RPG_GAME_CAPTION_MINIMIZE: SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
    case RPG_GAME_CAPTION_MAXIMIZE:
        SendMessageW(window, WM_SYSCOMMAND, IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE, 0);
        break;
    case RPG_GAME_CAPTION_CLOSE: SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0); break;
    default: break;
    }
}

/* A maximized overlapped window extends its resize frame outside the monitor.
   Keep that invisible frame outside raylib's client coordinates, otherwise the
   whole game image would begin above/left of the usable screen. */
static void CalculateClientArea(HWND window, NCCALCSIZE_PARAMS *parameters)
{
    MONITORINFO monitor = { .cbSize = sizeof(monitor) };
    HMONITOR target;
    if (parameters == NULL || !IsZoomed(window)) return;
    target = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (target != NULL && GetMonitorInfoW(target, &monitor)) parameters->rgrc[0] = monitor.rcWork;
}

static LRESULT CALLBACK GameWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCALCSIZE && wParam != FALSE) {
        CalculateClientArea(window, (NCCALCSIZE_PARAMS *)lParam);
        return 0;
    }
    if (message == WM_SIZE || message == WM_DPICHANGED) {
        LRESULT result = CallWindowProcW(originalWindowProcedure, window, message, wParam, lParam);
        UpdateCaptionBounds();
        return result;
    }
    if (message == WM_NCHITTEST) {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        LRESULT resize = HitTestResizeBorder(window, point);
        float x, y;
        if (resize != HTNOWHERE) return resize;
        if (!ScreenToClient(window, &point)) return HTCLIENT;
        if (!ClientPointToRaylibLogical(window, point, &x, &y)) return HTCLIENT;
        if (chromeVisible) {
            int button = GetCaptionButtonAt(x, y);
            if (button == RPG_GAME_CAPTION_MAXIMIZE) return HTMAXBUTTON;
            if (button == RPG_GAME_CAPTION_MINIMIZE || button == RPG_GAME_CAPTION_CLOSE) return HTCLIENT;
            if (y >= 0.0f && y < gameTitleHeight) return HTCAPTION;
        }
        return HTCLIENT;
    }
    if (message == WM_NCLBUTTONDOWN && wParam == HTMAXBUTTON) {
        pressedCaptionButton = RPG_GAME_CAPTION_MAXIMIZE;
        SetCapture(window);
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float x, y;
        int button = ClientPointToRaylibLogical(window, point, &x, &y) ? GetCaptionButtonAt(x, y) : -1;
        if (button >= 0) {
            pressedCaptionButton = button;
            SetCapture(window);
            return 0;
        }
    }
    if ((message == WM_LBUTTONUP || message == WM_NCLBUTTONUP) && pressedCaptionButton >= 0) {
        FinishCaptionPress(window);
        return 0;
    }
    return CallWindowProcW(originalWindowProcedure, window, message, wParam, lParam);
}

bool RpgGameWindow_Install(void *nativeWindow, float titleHeight)
{
    HWND window = (HWND)nativeWindow;
    if (window == NULL || gameWindow != NULL) return false;
    gameWindow = window;
    gameTitleHeight = titleHeight > 0.0f ? titleHeight : 40.0f;
    if (GetWindowTextA(window, gameWindowTitle, (int)sizeof(gameWindowTitle)) <= 0)
        lstrcpynA(gameWindowTitle, "1_44MB - RPG Version", (int)sizeof(gameWindowTitle));
    originalWindowProcedure = (WNDPROC)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)GameWindowProcedure);
    if (originalWindowProcedure == NULL) {
        gameWindow = NULL;
        return false;
    }
    /* A normal window owns an always-visible custom chrome.  Initializing its
       shared bounds here avoids requiring WM_SIZE before the first frame. */
    chromeVisible = !IsZoomed(window);
    UpdateCaptionBounds();
    SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

void RpgGameWindow_Uninstall(void)
{
    if (gameWindow != NULL && originalWindowProcedure != NULL)
        SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, (LONG_PTR)originalWindowProcedure);
    gameWindow = NULL;
    originalWindowProcedure = NULL;
    chromeVisible = false;
    pressedCaptionButton = -1;
}

void RpgGameWindow_UpdateAutoHide(void)
{
    float x, y;
    bool visible = gameWindow != NULL && !IsZoomed(gameWindow);
    if (!visible && gameWindow != NULL && GetCursorLogicalPosition(&x, &y))
        visible = y >= 0.0f && y <= gameTitleHeight + 12.0f;
    if (chromeVisible != visible) {
        chromeVisible = visible;
        UpdateCaptionBounds();
    }
}

void RpgGameWindow_DrawChrome(void)
{
    float x, y;
    Color foreground = (Color){ 30, 30, 30, 255 };
    if (!chromeVisible || gameWindow == NULL) return;
    DrawRectangle(0, 0, GetScreenWidth(), (int)ceilf(gameTitleHeight), RAYWHITE);
    DrawText(gameWindowTitle, 14, (int)((gameTitleHeight - 18.0f) * 0.5f), 18, foreground);
    (void)GetCursorLogicalPosition(&x, &y);
    for (int index = 0; index < 3; index++) {
        Rectangle bounds = captionBounds[index];
        bool hover = IsInside(bounds, x, y);
        Color background = index == RPG_GAME_CAPTION_CLOSE && hover ? (Color){ 196, 43, 28, 255 } :
                           hover ? Fade(BLACK, 0.07f) : BLANK;
        Color glyph = index == RPG_GAME_CAPTION_CLOSE && hover ? RAYWHITE : foreground;
        if (background.a > 0) DrawRectangleRec(bounds, background);
        float centerX = bounds.x + bounds.width * 0.5f;
        float centerY = bounds.y + bounds.height * 0.5f;
        if (index == RPG_GAME_CAPTION_MINIMIZE) DrawLineEx((Vector2){ centerX - 6.0f, centerY + 3.0f }, (Vector2){ centerX + 6.0f, centerY + 3.0f }, 1.4f, glyph);
        else if (index == RPG_GAME_CAPTION_MAXIMIZE) {
            if (IsZoomed(gameWindow)) {
                DrawRectangleLinesEx((Rectangle){ centerX - 4.0f, centerY - 5.0f, 8.0f, 8.0f }, 1.2f, glyph);
                DrawRectangleLinesEx((Rectangle){ centerX - 6.0f, centerY - 3.0f, 8.0f, 8.0f }, 1.2f, glyph);
            } else DrawRectangleLinesEx((Rectangle){ centerX - 5.0f, centerY - 5.0f, 10.0f, 10.0f }, 1.2f, glyph);
        } else {
            DrawLineEx((Vector2){ centerX - 5.0f, centerY - 5.0f }, (Vector2){ centerX + 5.0f, centerY + 5.0f }, 1.4f, glyph);
            DrawLineEx((Vector2){ centerX + 5.0f, centerY - 5.0f }, (Vector2){ centerX - 5.0f, centerY + 5.0f }, 1.4f, glyph);
        }
    }
}

#else
bool RpgGameWindow_Install(void *nativeWindow, float titleHeight) { (void)nativeWindow; (void)titleHeight; return false; }
void RpgGameWindow_Uninstall(void) { }
void RpgGameWindow_UpdateAutoHide(void) { }
void RpgGameWindow_DrawChrome(void) { }
#endif
