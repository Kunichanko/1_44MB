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

/* Zipper は固定フォルダではなく、現在採用中のフォルダ構造を開く。 */
static char activeZipperDirectory[1200] = { 0 };

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

/* Folder配置物の実体はビルド初期化で消える場合があるため、開く直前に空フォルダだけ復元する。 */
static bool EnsureDirectoryExists(const wchar_t *directory)
{
    DWORD attributes = GetFileAttributesW(directory);
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return CreateDirectoryW(directory, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool OpenWindowsExplorer(const wchar_t *directory)
{
    return (INT_PTR)ShellExecuteW(NULL, L"open", directory, NULL, NULL, RPG_EXPLORER_SHOW_NORMAL) > 32;
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

void RpgExplorerLauncher_SetZipperDirectory(const char *path)
{
    if (path == NULL || path[0] == '\0') activeZipperDirectory[0] = '\0';
    else snprintf(activeZipperDirectory, sizeof(activeZipperDirectory), "%s", path);
}

bool RpgExplorerLauncher_GetZipperDirectory(char *path, size_t pathSize)
{
#ifdef _WIN32
    if (path == NULL || pathSize == 0) return false;
    if (activeZipperDirectory[0] != '\0')
        return snprintf(path, pathSize, "%s", activeZipperDirectory) > 0;
    return GetZipperDirectory(path, pathSize);
#else
    (void)path;
    (void)pathSize;
    return false;
#endif
}

bool RpgExplorerLauncher_OpenZipperDirectory(void)
{
#ifdef _WIN32
    char directory[1200];
    wchar_t wideDirectory[1200];
    EnsureSettingsDirectory();
    if (!RpgExplorerLauncher_GetZipperDirectory(directory, sizeof(directory)) ||
        !ToAbsoluteWide(directory, wideDirectory, (int)(sizeof(wideDirectory) / sizeof(wideDirectory[0])))) return false;
    if (RpgExplorerLauncher_LoadMode() == RPG_EXPLORER_MODE_WINDOWS)
        return OpenWindowsExplorer(wideDirectory);
    return OpenVirtualExplorer(wideDirectory) || OpenWindowsExplorer(wideDirectory);
#else
    return false;
#endif
}

bool RpgExplorerLauncher_OpenDirectory(const char *path)
{
#ifdef _WIN32
    wchar_t wideDirectory[1200];
    if (!ToAbsoluteWide(path, wideDirectory, (int)(sizeof(wideDirectory) / sizeof(wideDirectory[0]))) ||
        !EnsureDirectoryExists(wideDirectory)) return false;
    /* build/cells・objectsなどはZipper専用UIの対象外。実Explorerで直接開き、
       プレイ中でもbuild配下の生成物を確実に確認・操作できるようにする。 */
    return OpenWindowsExplorer(wideDirectory);
#else
    (void)path;
    return false;
#endif
}
