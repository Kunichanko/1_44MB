// 依存する自プロジェクト内ファイル: rpg_explorer_window.h。
// 役割: raylibのclient-area title barに対し、Win32の移動・リサイズ・Snap Layouts操作を橋渡しする。
#include "rpg_explorer_window.h"

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
#include <dwmapi.h>
#undef LoadImage
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle

/* mingw の既定ターゲットでも Windows 11 の公開 User32 API を使えるよう宣言する。 */
extern UINT WINAPI GetDpiForWindow(HWND window);
extern int WINAPI GetSystemMetricsForDpi(int index, UINT dpi);

static HWND explorerWindow = NULL;
static WNDPROC originalWindowProcedure = NULL;
static Rectangle tabBounds = { 8.0f, 4.0f, 276.0f, 28.0f };
static float titleBarHeight = 32.0f;
static float captionButtonWidth = 46.0f;
static Rectangle captionButtonBounds = { 0 };
static Rectangle minimizeButtonBounds = { 0 };
static Rectangle maximizeButtonBounds = { 0 };
static Rectangle closeButtonBounds = { 0 };
static bool captionLayoutComesFromUi = false;
static int pressedCaptionButton = -1;

static float WindowDpiScale(HWND window);

static void StoreCaptionButtonLayout(Rectangle bounds)
{
    captionButtonBounds = bounds;
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        minimizeButtonBounds = (Rectangle){ 0 };
        maximizeButtonBounds = (Rectangle){ 0 };
        closeButtonBounds = (Rectangle){ 0 };
        return;
    }
    minimizeButtonBounds = (Rectangle){ bounds.x, bounds.y, captionButtonWidth, bounds.height };
    maximizeButtonBounds = (Rectangle){ bounds.x + captionButtonWidth, bounds.y,
                                         captionButtonWidth, bounds.height };
    closeButtonBounds = (Rectangle){ bounds.x + captionButtonWidth * 2.0f, bounds.y,
                                      captionButtonWidth, bounds.height };
}

static float WindowDpiScale(HWND window)
{
    UINT dpi = GetDpiForWindow(window);
    return dpi > 0 ? (float)dpi / 96.0f : 1.0f;
}

static void EnsureSystemCaptionStyles(HWND window)
{
    const LONG_PTR required = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if ((style & required) != required) SetWindowLongPtrW(window, GWL_STYLE, style | required);
}

static void ExtendDwmFrame(HWND window)
{
    /* 自作 caption の描画領域を DWM frame で覆わないよう、上端の拡張は行わない。 */
    /* 角丸・影をDWMに任せつつ、raylibのclient背景を上端まで描画する。caption buttonはraylibが描く。 */
    MARGINS margins = { 0, 0, 0, 0 };
    (void)DwmExtendFrameIntoClientArea(window, &margins);
}

static void CalculateClientArea(HWND window, NCCALCSIZE_PARAMS *parameters)
{
    MONITORINFO monitor = { .cbSize = sizeof(monitor) };
    HMONITOR targetMonitor;

    if (parameters == NULL || !IsZoomed(window)) return;

    /* 最大化した厚いframeは作業領域の外へ張り出す。そこをclientへ含めると、
       raylibのlogical原点まで画面外へ出るため、OSが通知する作業領域をclientにする。 */
    targetMonitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (targetMonitor != NULL && GetMonitorInfoW(targetMonitor, &monitor)) {
        parameters->rgrc[0] = monitor.rcWork;
    }
}

static void UpdateCaptionLayout(HWND window)
{
    RECT client;
    float scale;
    if (captionLayoutComesFromUi) return;
    if (window == NULL || !GetClientRect(window, &client)) {
        StoreCaptionButtonLayout((Rectangle){ 0 });
        return;
    }
    scale = WindowDpiScale(window);
    StoreCaptionButtonLayout((Rectangle){ ((float)(client.right - client.left) / scale) - captionButtonWidth * 3.0f,
                                           0.0f, captionButtonWidth * 3.0f, titleBarHeight });
}

static bool IsInside(Rectangle bounds, float x, float y)
{
    return x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width && y < bounds.y + bounds.height;
}

static int CaptionButtonAtLogical(float logicalX, float logicalY)
{
    if (IsInside(minimizeButtonBounds, logicalX, logicalY)) return RPG_EXPLORER_CAPTION_MINIMIZE;
    if (IsInside(maximizeButtonBounds, logicalX, logicalY)) return RPG_EXPLORER_CAPTION_MAXIMIZE;
    if (IsInside(closeButtonBounds, logicalX, logicalY)) return RPG_EXPLORER_CAPTION_CLOSE;
    return -1;
}

static bool GetCursorLogicalPosition(HWND window, float *logicalX, float *logicalY)
{
    POINT point;
    float scale;
    if (window == NULL || logicalX == NULL || logicalY == NULL || !GetCursorPos(&point) ||
        !ScreenToClient(window, &point)) return false;
    scale = WindowDpiScale(window);
    *logicalX = (float)point.x / scale;
    *logicalY = (float)point.y / scale;
    return true;
}

static void BeginCaptionPress(HWND window, RpgExplorerCaptionButton button)
{
    pressedCaptionButton = (int)button;
    SetCapture(window);
}

static void FinishCaptionPress(HWND window)
{
    int button = pressedCaptionButton;
    float logicalX, logicalY;
    pressedCaptionButton = -1;
    if (GetCapture() == window) ReleaseCapture();
    if (button < 0 || !GetCursorLogicalPosition(window, &logicalX, &logicalY) ||
        CaptionButtonAtLogical(logicalX, logicalY) != button) return;

    switch ((RpgExplorerCaptionButton)button) {
    case RPG_EXPLORER_CAPTION_MINIMIZE:
        SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        break;
    case RPG_EXPLORER_CAPTION_MAXIMIZE:
        SendMessageW(window, WM_SYSCOMMAND, IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE, 0);
        break;
    case RPG_EXPLORER_CAPTION_CLOSE:
        SendMessageW(window, WM_SYSCOMMAND, SC_CLOSE, 0);
        break;
    default:
        break;
    }
}

static LRESULT HitTestResizeBorder(HWND window, POINT point)
{
    RECT frame;
    UINT dpi = GetDpiForWindow(window);
    int border;
    bool left, right, top, bottom;
    if (IsZoomed(window) || (GetWindowLongPtrW(window, GWL_STYLE) & WS_THICKFRAME) == 0 ||
        !GetWindowRect(window, &frame)) return HTNOWHERE;
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

static LRESULT HitTestCaptionButtons(HWND window, float logicalX, float logicalY)
{
    (void)window;
    if (IsInside(minimizeButtonBounds, logicalX, logicalY)) return HTCLIENT;
    if (IsInside(maximizeButtonBounds, logicalX, logicalY)) {
        /* Snap Layoutsはraylibのhover状態ではなく、OSの問い合わせごとに常に返す。 */
        return HTMAXBUTTON;
    }
    if (IsInside(closeButtonBounds, logicalX, logicalY)) return HTCLIENT;
    return HTNOWHERE;
}

static bool IsCaptionSystemCommand(WPARAM command)
{
    WPARAM systemCommand = command & 0xFFF0;
    return systemCommand == SC_MINIMIZE || systemCommand == SC_MAXIMIZE ||
           systemCommand == SC_RESTORE || systemCommand == SC_CLOSE;
}

static LRESULT CALLBACK ExplorerWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT result;
    if (message == WM_NCCALCSIZE && wParam != FALSE) {
        /* 通常時はclient-area title barを維持し、最大化時だけ画面外のresize frameを除く。 */
        CalculateClientArea(window, (NCCALCSIZE_PARAMS *)lParam);
        return 0;
    }

    if (message == WM_ACTIVATE || message == WM_DWMCOMPOSITIONCHANGED ||
        message == WM_DPICHANGED || message == WM_SIZE) {
        result = CallWindowProcW(originalWindowProcedure, window, message, wParam, lParam);
        ExtendDwmFrame(window);
        UpdateCaptionLayout(window);
        return result;
    }

    if (message == WM_NCHITTEST) {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        LRESULT borderResult = HitTestResizeBorder(window, point);
        if (borderResult != HTNOWHERE) return borderResult;

        if (ScreenToClient(window, &point)) {
            float scale = WindowDpiScale(window);
            float logicalX = (float)point.x / scale;
            float logicalY = (float)point.y / scale;
            LRESULT captionResult = HitTestCaptionButtons(window, logicalX, logicalY);
            if (captionResult != HTNOWHERE) return captionResult;
            if (logicalY >= 0.0f && logicalY < titleBarHeight) {
                Rectangle titleActionBounds = tabBounds;
                titleActionBounds.width += 48.0f;
                if (IsInside(titleActionBounds, logicalX, logicalY)) return HTCLIENT;
                return HTCAPTION;
            }
        }
        return HTCLIENT;
    }

    if (message == WM_NCLBUTTONDOWN && wParam == HTMAXBUTTON) {
        /* Snap Layouts用のHTMAXBUTTONは残し、押下追跡だけをWindows標準captionから切り離す。 */
        BeginCaptionPress(window, RPG_EXPLORER_CAPTION_MAXIMIZE);
        return 0;
    }
    if (message == WM_LBUTTONDOWN) {
        float scale = WindowDpiScale(window);
        float logicalX = (float)GET_X_LPARAM(lParam) / scale;
        float logicalY = (float)GET_Y_LPARAM(lParam) / scale;
        int button = CaptionButtonAtLogical(logicalX, logicalY);
        if (button >= 0) {
            BeginCaptionPress(window, (RpgExplorerCaptionButton)button);
            return 0;
        }
    }
    if ((message == WM_LBUTTONUP || message == WM_NCLBUTTONUP) && pressedCaptionButton >= 0) {
        FinishCaptionPress(window);
        return 0;
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) pressedCaptionButton = -1;
    if (message == WM_SYSCOMMAND && IsCaptionSystemCommand(wParam)) {
        /* SC_CLOSEはDefWindowProcからWM_CLOSEになり、次の通常経路でraylibへ渡る。 */
        return DefWindowProcW(window, message, wParam, lParam);
    }

    return CallWindowProcW(originalWindowProcedure, window, message, wParam, lParam);
}

bool RpgExplorerWindow_Install(void *nativeWindow, float requestedTitleBarHeight)
{
    LONG_PTR previousProcedure;
    if (nativeWindow == NULL || explorerWindow != NULL) return false;
    explorerWindow = (HWND)nativeWindow;
    captionLayoutComesFromUi = false;
    pressedCaptionButton = -1;
    titleBarHeight = requestedTitleBarHeight;
    EnsureSystemCaptionStyles(explorerWindow);
    SetLastError(0);
    previousProcedure = SetWindowLongPtrW(explorerWindow, GWLP_WNDPROC, (LONG_PTR)ExplorerWindowProcedure);
    if (previousProcedure == 0 && GetLastError() != 0) {
        explorerWindow = NULL;
        return false;
    }
    originalWindowProcedure = (WNDPROC)previousProcedure;
    ExtendDwmFrame(explorerWindow);
    SetWindowPos(explorerWindow, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    /* raylib の最初の framebuffer を実際の resize と同じ経路で確定させる。 */
    {
        RECT frame;
        if (GetWindowRect(explorerWindow, &frame)) {
            int width = frame.right - frame.left;
            int height = frame.bottom - frame.top;
            SetWindowPos(explorerWindow, NULL, frame.left, frame.top, width + 1, height + 1,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(explorerWindow, NULL, frame.left, frame.top, width, height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    /* WM_SIZE待ちにせず、最初のBeginDrawingより前に同じlogical矩形を確定する。 */
    UpdateCaptionLayout(explorerWindow);
    return true;
}

void RpgExplorerWindow_Uninstall(void)
{
    if (explorerWindow != NULL && originalWindowProcedure != NULL) {
        MARGINS margins = { 0, 0, 0, 0 };
        (void)DwmExtendFrameIntoClientArea(explorerWindow, &margins);
        SetWindowLongPtrW(explorerWindow, GWLP_WNDPROC, (LONG_PTR)originalWindowProcedure);
        SetWindowPos(explorerWindow, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    explorerWindow = NULL;
    originalWindowProcedure = NULL;
    captionLayoutComesFromUi = false;
    pressedCaptionButton = -1;
    StoreCaptionButtonLayout((Rectangle){ 0 });
}

void RpgExplorerWindow_SetTabBounds(Rectangle bounds)
{
    tabBounds = bounds;
}

void RpgExplorerWindow_SetCaptionButtonWidth(float logicalWidth)
{
    if (logicalWidth > 0.0f) {
        captionButtonWidth = logicalWidth;
        if (!captionLayoutComesFromUi) UpdateCaptionLayout(explorerWindow);
    }
}

void RpgExplorerWindow_SetCaptionButtonLayout(Rectangle bounds)
{
    /* raylib の logical 座標で作った唯一の caption 矩形を描画・入力・Win32判定で共有する。 */
    if (bounds.width > 0.0f && bounds.height > 0.0f) {
        StoreCaptionButtonLayout(bounds);
        captionLayoutComesFromUi = true;
    }
}

Rectangle RpgExplorerWindow_GetCaptionButtonBounds(void)
{
    return captionButtonBounds;
}

Rectangle RpgExplorerWindow_GetCaptionButtonRect(RpgExplorerCaptionButton button)
{
    switch (button) {
    case RPG_EXPLORER_CAPTION_MINIMIZE: return minimizeButtonBounds;
    case RPG_EXPLORER_CAPTION_MAXIMIZE: return maximizeButtonBounds;
    case RPG_EXPLORER_CAPTION_CLOSE: return closeButtonBounds;
    default: return (Rectangle){ 0 };
    }
}

bool RpgExplorerWindow_IsCaptionButtonHovered(RpgExplorerCaptionButton button)
{
    float logicalX, logicalY;
    return GetCursorLogicalPosition(explorerWindow, &logicalX, &logicalY) &&
           CaptionButtonAtLogical(logicalX, logicalY) == (int)button;
}

bool RpgExplorerWindow_IsCaptionButtonPressed(RpgExplorerCaptionButton button)
{
    return pressedCaptionButton == (int)button;
}

bool RpgExplorerWindow_IsMaximized(void) { return explorerWindow != NULL && IsZoomed(explorerWindow) != FALSE; }
#else
bool RpgExplorerWindow_Install(void *nativeWindow, float requestedTitleBarHeight)
{ (void)nativeWindow; (void)requestedTitleBarHeight; return false; }
void RpgExplorerWindow_Uninstall(void) { }
void RpgExplorerWindow_SetTabBounds(Rectangle bounds) { (void)bounds; }
void RpgExplorerWindow_SetCaptionButtonWidth(float logicalWidth) { (void)logicalWidth; }
void RpgExplorerWindow_SetCaptionButtonLayout(Rectangle bounds) { (void)bounds; }
Rectangle RpgExplorerWindow_GetCaptionButtonBounds(void) { return (Rectangle){ 0 }; }
Rectangle RpgExplorerWindow_GetCaptionButtonRect(RpgExplorerCaptionButton button) { (void)button; return (Rectangle){ 0 }; }
bool RpgExplorerWindow_IsCaptionButtonHovered(RpgExplorerCaptionButton button) { (void)button; return false; }
bool RpgExplorerWindow_IsCaptionButtonPressed(RpgExplorerCaptionButton button) { (void)button; return false; }
bool RpgExplorerWindow_IsMaximized(void) { return false; }
#endif
