// 依存する自プロジェクト内ファイル: rpg_explorer_launcher.h
// 役割: 保存済みの起動先設定を読み、Zipper フォルダを選択された Explorer で別ウィンドウ表示する。
#include "rpg_explorer_launcher.h"

#include <stdio.h>
#include <string.h>

#include "raylib.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <shellapi.h>
#define RPG_EXPLORER_SHOW_NORMAL 1

static bool ToWide(const char *path, wchar_t *wide, int count)
{
    return path != NULL && MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count) > 0;
}

static bool ToAbsoluteWide(const char *path, wchar_t *wide, int count)
{
    wchar_t input[1200];
    DWORD length;
    if (!ToWide(path, input, (int)(sizeof(input) / sizeof(input[0])))) return false;
    length = GetFullPathNameW(input, (DWORD)count, wide, NULL);
    return length > 0 && length < (DWORD)count;
}

static bool GetZipperDirectory(char *path, size_t size)
{
    return snprintf(path, size, "%s../assets/Settings/Zipper", GetApplicationDirectory()) > 0;
}

static bool GetSettingsPath(char *path, size_t size)
{
    return snprintf(path, size, "%s../assets/Settings/rpg_explorer.cfg", GetApplicationDirectory()) > 0;
}

static void EnsureSettingsDirectory(void)
{
    char settings[1200], zipper[1200];
    wchar_t wideSettings[1200], wideZipper[1200];
    if (snprintf(settings, sizeof(settings), "%s../assets/Settings", GetApplicationDirectory()) <= 0 ||
        !GetZipperDirectory(zipper, sizeof(zipper)) ||
        !ToAbsoluteWide(settings, wideSettings, (int)(sizeof(wideSettings) / sizeof(wideSettings[0]))) ||
        !ToAbsoluteWide(zipper, wideZipper, (int)(sizeof(wideZipper) / sizeof(wideZipper[0])))) return;
    CreateDirectoryW(wideSettings, NULL);
    CreateDirectoryW(wideZipper, NULL);
}

static bool OpenVirtualExplorer(const wchar_t *wideDirectory)
{
    char executable[1200];
    wchar_t wideExecutable[1200], commandLine[2400];
    STARTUPINFOW startup = { .cb = sizeof(startup) };
    PROCESS_INFORMATION process = { 0 };
    if (snprintf(executable, sizeof(executable), "%srpg_explorer.exe", GetApplicationDirectory()) <= 0 ||
        !ToAbsoluteWide(executable, wideExecutable, (int)(sizeof(wideExecutable) / sizeof(wideExecutable[0]))) ||
        swprintf(commandLine, sizeof(commandLine) / sizeof(commandLine[0]), L"\"%ls\" \"%ls\"", wideExecutable, wideDirectory) < 0 ||
        CreateProcessW(wideExecutable, commandLine, NULL, NULL, FALSE, 0, NULL, wideDirectory, &startup, &process) == 0) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
#endif

RpgExplorerMode RpgExplorerLauncher_LoadMode(void)
{
#ifdef _WIN32
    char path[1200], value[32] = { 0 };
    FILE *file;
    if (!GetSettingsPath(path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return RPG_EXPLORER_MODE_VIRTUAL;
    (void)fgets(value, sizeof(value), file);
    fclose(file);
    return strncmp(value, "windows", 7) == 0 ? RPG_EXPLORER_MODE_WINDOWS : RPG_EXPLORER_MODE_VIRTUAL;
#else
    return RPG_EXPLORER_MODE_VIRTUAL;
#endif
}

bool RpgExplorerLauncher_SaveMode(RpgExplorerMode mode)
{
#ifdef _WIN32
    char path[1200];
    FILE *file;
    EnsureSettingsDirectory();
    if (!GetSettingsPath(path, sizeof(path)) || (file = fopen(path, "wb")) == NULL) return false;
    fputs(mode == RPG_EXPLORER_MODE_WINDOWS ? "windows\n" : "virtual\n", file);
    return fclose(file) == 0;
#else
    (void)mode;
    return false;
#endif
}

bool RpgExplorerLauncher_OpenZipperDirectory(void)
{
#ifdef _WIN32
    char directory[1200];
    wchar_t wideDirectory[1200];
    EnsureSettingsDirectory();
    if (!GetZipperDirectory(directory, sizeof(directory)) ||
        !ToAbsoluteWide(directory, wideDirectory, (int)(sizeof(wideDirectory) / sizeof(wideDirectory[0])))) return false;
    if (RpgExplorerLauncher_LoadMode() == RPG_EXPLORER_MODE_WINDOWS)
        return (INT_PTR)ShellExecuteW(NULL, L"open", wideDirectory, NULL, NULL, RPG_EXPLORER_SHOW_NORMAL) > 32;
    return OpenVirtualExplorer(wideDirectory);
#else
    return false;
#endif
}
