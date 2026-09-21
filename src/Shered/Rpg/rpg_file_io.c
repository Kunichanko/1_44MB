// 役割: Windows UnicodeファイルAPIを使い、外部ファイル読込の文字コード差異を吸収する。
// 依存する自プロジェクト内ファイル: rpg_file_io.h。
#include "rpg_file_io.h"

#include <limits.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>

bool RpgFileIo_Utf8ToWide(const char *source, wchar_t *destination, int destinationCount)
{
    return source != NULL && destination != NULL && destinationCount > 0 &&
           MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, destination,
                               destinationCount) > 0;
}

bool RpgFileIo_WideToUtf8(const wchar_t *source, char *destination, int destinationCount)
{
    return source != NULL && destination != NULL && destinationCount > 0 &&
           WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, destinationCount,
                               NULL, NULL) > 0;
}

bool RpgFileIo_ResolveAssetPath(const char *assetDirectory, const char *storedPath,
                                char *absolutePath, int absolutePathSize)
{
    wchar_t sourceWide[1200] = {0};
    wchar_t assetWide[128] = {0};
    wchar_t modulePath[1200] = {0};
    wchar_t combinedPath[1200] = {0};
    wchar_t resolvedPath[1200] = {0};
    wchar_t *fileName;
    if (assetDirectory == NULL || assetDirectory[0] == '\0' || storedPath == NULL ||
        storedPath[0] == '\0' || absolutePath == NULL || absolutePathSize <= 0 ||
        !RpgFileIo_Utf8ToWide(storedPath, sourceWide,
                               (int)(sizeof(sourceWide) / sizeof(sourceWide[0]))) ||
        !RpgFileIo_Utf8ToWide(assetDirectory, assetWide,
                               (int)(sizeof(assetWide) / sizeof(assetWide[0])))) return false;
    if (sourceWide[1] == L':' || (sourceWide[0] == L'\\' && sourceWide[1] == L'\\'))
        return GetFullPathNameW(sourceWide, (DWORD)(sizeof(resolvedPath) / sizeof(resolvedPath[0])),
                                resolvedPath, NULL) > 0 &&
               RpgFileIo_WideToUtf8(resolvedPath, absolutePath, absolutePathSize);
    if (GetModuleFileNameW(NULL, modulePath, (DWORD)(sizeof(modulePath) / sizeof(modulePath[0]))) == 0 ||
        (fileName = wcsrchr(modulePath, L'\\')) == NULL) return false;
    *fileName = L'\0';
    if (swprintf(combinedPath, sizeof(combinedPath) / sizeof(combinedPath[0]), L"%ls\\..\\assets\\%ls\\%ls",
                 modulePath, assetWide, sourceWide) < 0) return false;
    return GetFullPathNameW(combinedPath, (DWORD)(sizeof(resolvedPath) / sizeof(resolvedPath[0])),
                            resolvedPath, NULL) > 0 &&
           RpgFileIo_WideToUtf8(resolvedPath, absolutePath, absolutePathSize);
}

bool RpgFileIo_ReadAllBytesUtf8(const char *path, size_t maximumSize,
                                unsigned char **bytes, int *byteCount)
{
    wchar_t widePath[1200];
    LARGE_INTEGER size;
    DWORD readSize = 0;
    HANDLE file;
    unsigned char *buffer;
    if (bytes == NULL || byteCount == NULL || maximumSize == 0 ||
        !RpgFileIo_Utf8ToWide(path, widePath, (int)(sizeof(widePath) / sizeof(widePath[0])))) return false;
    *bytes = NULL;
    *byteCount = 0;
    file = CreateFileW(widePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        (unsigned long long)size.QuadPart > maximumSize || size.QuadPart > INT_MAX) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return false;
    }
    buffer = malloc((size_t)size.QuadPart);
    if (buffer == NULL || !ReadFile(file, buffer, (DWORD)size.QuadPart, &readSize, NULL) ||
        readSize != (DWORD)size.QuadPart) {
        free(buffer);
        CloseHandle(file);
        return false;
    }
    CloseHandle(file);
    *bytes = buffer;
    *byteCount = (int)size.QuadPart;
    return true;
}
#else
bool RpgFileIo_ReadAllBytesUtf8(const char *path, size_t maximumSize,
                                unsigned char **bytes, int *byteCount)
{
    (void)path; (void)maximumSize; (void)bytes; (void)byteCount;
    return false;
}
#endif

void RpgFileIo_FreeBytes(unsigned char *bytes) { free(bytes); }
