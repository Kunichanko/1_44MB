// 依存する自プロジェクト内ファイル: rpg_explorer_shell.h。
// 役割: Shell API の HICON を raylib 画像へ変換し、一覧の更新時だけ再利用可能な Texture を作る。
#include "rpg_explorer_shell.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#include <windows.h>
#include <shellapi.h>
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle

static bool Utf8ToWide(const char *text, wchar_t *wide, int count)
{
    return text != NULL && MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count) > 0;
}

static bool WideToUtf8(const wchar_t *wide, char *text, int count)
{
    return wide != NULL && WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, count, NULL, NULL) > 0;
}

static void MakeIconKey(const RpgExplorerEntry *entry, char *key, size_t size)
{
    const char *extension = strrchr(entry->name, '.');
    if (entry->isDirectory) snprintf(key, size, "folder");
    else if (extension != NULL) snprintf(key, size, "extension:%s", extension);
    else snprintf(key, size, "file");
    for (char *cursor = key; *cursor != '\0'; cursor++) *cursor = (char)tolower((unsigned char)*cursor);
}

static Texture2D ConvertIconToTexture(HICON icon)
{
    ICONINFO information;
    BITMAP bitmap;
    BITMAPINFO header = { 0 };
    HDC screen;
    unsigned char *pixels;
    Image image = { 0 };
    Texture2D texture = { 0 };
    if (icon == NULL || GetIconInfo(icon, &information) == 0) return texture;
    if (information.hbmColor == NULL || GetObjectW(information.hbmColor, sizeof(bitmap), &bitmap) == 0) {
        if (information.hbmColor != NULL) DeleteObject(information.hbmColor);
        if (information.hbmMask != NULL) DeleteObject(information.hbmMask);
        return texture;
    }
    pixels = MemAlloc((size_t)bitmap.bmWidth * (size_t)bitmap.bmHeight * 4U);
    if (pixels != NULL) {
        header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        header.bmiHeader.biWidth = bitmap.bmWidth;
        header.bmiHeader.biHeight = -bitmap.bmHeight;
        header.bmiHeader.biPlanes = 1;
        header.bmiHeader.biBitCount = 32;
        header.bmiHeader.biCompression = BI_RGB;
        screen = GetDC(NULL);
        if (GetDIBits(screen, information.hbmColor, 0, (UINT)bitmap.bmHeight, pixels, &header, DIB_RGB_COLORS) != 0) {
            for (int index = 0; index < bitmap.bmWidth * bitmap.bmHeight; index++) {
                unsigned char blue = pixels[index * 4 + 0];
                pixels[index * 4 + 0] = pixels[index * 4 + 2];
                pixels[index * 4 + 2] = blue;
            }
            image = (Image){ .data = pixels, .width = bitmap.bmWidth, .height = bitmap.bmHeight,
                             .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            texture = LoadTextureFromImage(image);
        }
        ReleaseDC(NULL, screen);
        MemFree(pixels);
    }
    DeleteObject(information.hbmColor);
    DeleteObject(information.hbmMask);
    return texture;
}

Texture2D RpgExplorerShell_LoadFolderIconTexture(void)
{
    SHFILEINFOW information = { 0 };
    Texture2D texture = { 0 };
    if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &information, sizeof(information),
                       SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) == 0) return texture;
    texture = ConvertIconToTexture(information.hIcon);
    DestroyIcon(information.hIcon);
    return texture;
}

Texture2D RpgExplorerShell_LoadFileIconTexture(void)
{
    SHFILEINFOW information = { 0 };
    Texture2D texture = { 0 };
    if (SHGetFileInfoW(L"file.txt", FILE_ATTRIBUTE_NORMAL, &information, sizeof(information),
                       SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) == 0) return texture;
    texture = ConvertIconToTexture(information.hIcon);
    DestroyIcon(information.hIcon);
    return texture;
}

static int FindOrAddIcon(RpgExplorerShellCache *cache, const RpgExplorerEntry *entry)
{
    char key[RPG_EXPLORER_TYPE_LENGTH];
    wchar_t widePath[RPG_EXPLORER_PATH_LENGTH];
    SHFILEINFOW information = { 0 };
    MakeIconKey(entry, key, sizeof(key));
    for (int index = 0; index < cache->count; index++) if (strcmp(cache->icons[index].key, key) == 0) return index;
    if (cache->count >= RPG_EXPLORER_ICON_CACHE_CAPACITY || !Utf8ToWide(entry->path, widePath, RPG_EXPLORER_PATH_LENGTH)) return -1;
    if (SHGetFileInfoW(widePath, entry->isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &information,
                       sizeof(information), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) == 0) return -1;
    int slot = cache->count++;
    snprintf(cache->icons[slot].key, sizeof(cache->icons[slot].key), "%s", key);
    cache->icons[slot].texture = ConvertIconToTexture(information.hIcon);
    DestroyIcon(information.hIcon);
    return slot;
}

void RpgExplorerShell_ResolveEntries(RpgExplorerShellCache *cache, RpgExplorerFilesystem *filesystem)
{
    if (cache == NULL || filesystem == NULL) return;
    /* Zipper と配下ツリーは UI glyph ではなく、Windows Shell の実フォルダーアイコンを使う。 */
    if (cache->folderIconSlot < 0) {
        RpgExplorerEntry root = { 0 };
        root.isDirectory = true;
        snprintf(root.path, sizeof(root.path), "%s", filesystem->rootPath);
        snprintf(root.name, sizeof(root.name), "Zipper");
        cache->folderIconSlot = FindOrAddIcon(cache, &root);
    }
    for (int index = 0; index < filesystem->entryCount; index++) {
        RpgExplorerEntry *entry = &filesystem->entries[index];
        wchar_t widePath[RPG_EXPLORER_PATH_LENGTH];
        SHFILEINFOW information = { 0 };
        entry->iconSlot = FindOrAddIcon(cache, entry);
        if (Utf8ToWide(entry->path, widePath, RPG_EXPLORER_PATH_LENGTH) &&
            SHGetFileInfoW(widePath, entry->isDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &information,
                            sizeof(information), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) != 0)
            WideToUtf8(information.szTypeName, entry->typeName, sizeof(entry->typeName));
    }
}

Texture2D RpgExplorerShell_GetTexture(const RpgExplorerShellCache *cache, int slot)
{
    return cache != NULL && slot >= 0 && slot < cache->count ? cache->icons[slot].texture : (Texture2D){ 0 };
}

Texture2D RpgExplorerShell_GetFolderTexture(const RpgExplorerShellCache *cache)
{
    return RpgExplorerShell_GetTexture(cache, cache != NULL ? cache->folderIconSlot : -1);
}

void RpgExplorerShell_Unload(RpgExplorerShellCache *cache)
{
    if (cache == NULL) return;
    for (int index = 0; index < cache->count; index++) if (cache->icons[index].texture.id != 0) UnloadTexture(cache->icons[index].texture);
    memset(cache, 0, sizeof(*cache));
}
#else
void RpgExplorerShell_ResolveEntries(RpgExplorerShellCache *cache, RpgExplorerFilesystem *filesystem)
{ (void)cache; (void)filesystem; }
Texture2D RpgExplorerShell_GetTexture(const RpgExplorerShellCache *cache, int slot)
{ (void)cache; (void)slot; return (Texture2D){ 0 }; }
Texture2D RpgExplorerShell_GetFolderTexture(const RpgExplorerShellCache *cache)
{ (void)cache; return (Texture2D){ 0 }; }
Texture2D RpgExplorerShell_LoadFolderIconTexture(void) { return (Texture2D){ 0 }; }
Texture2D RpgExplorerShell_LoadFileIconTexture(void) { return (Texture2D){ 0 }; }
void RpgExplorerShell_Unload(RpgExplorerShellCache *cache) { (void)cache; }
#endif
