// 依存: file_dialog.h
#include "file_dialog.h"

/* 依存関係を更新: 共有RPGモジュールの配置に合わせる。 */
#include "../Shered/Rpg/rpg_file_io.h"

#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static bool ResolveBaseDirectory(const FileDialogRequest *request, wchar_t *baseDirectory,
                                 int baseDirectoryCount)
{
    wchar_t requestedBase[1024] = {0};
    if (request == NULL || request->baseDirectory == NULL || request->baseDirectory[0] == '\0' ||
        baseDirectory == NULL || baseDirectoryCount <= 0 ||
        !RpgFileIo_Utf8ToWide(request->baseDirectory, requestedBase,
                               (int)(sizeof(requestedBase) / sizeof(requestedBase[0])))) return false;
    return GetFullPathNameW(requestedBase, (DWORD)baseDirectoryCount, baseDirectory, NULL) > 0;
}

bool FileDialog_ResolvePath(const FileDialogRequest *request, const char *relativePath,
                            char *absolutePath, size_t absolutePathSize)
{
    wchar_t baseDirectory[1024] = {0}, storedPath[1024] = {0};
    wchar_t combinedPath[1024] = {0}, resolvedPath[1024] = {0};
    if (absolutePath == NULL || absolutePathSize == 0 || relativePath == NULL || relativePath[0] == '\0' ||
        !ResolveBaseDirectory(request, baseDirectory, (int)(sizeof(baseDirectory) / sizeof(baseDirectory[0]))) ||
        !RpgFileIo_Utf8ToWide(relativePath, storedPath, (int)(sizeof(storedPath) / sizeof(storedPath[0])))) return false;
    if (PathIsRelativeW(storedPath)) {
        if (PathCombineW(combinedPath, baseDirectory, storedPath) == NULL) return false;
    } else if (wcsncpy_s(combinedPath, sizeof(combinedPath) / sizeof(combinedPath[0]), storedPath, _TRUNCATE) != 0) return false;
    return GetFullPathNameW(combinedPath, (DWORD)(sizeof(resolvedPath) / sizeof(resolvedPath[0])), resolvedPath, NULL) > 0 &&
           RpgFileIo_WideToUtf8(resolvedPath, absolutePath, (int)absolutePathSize);
}

bool FileDialog_Select(char *relativePath, size_t relativePathSize, const FileDialogRequest *request)
{
    wchar_t selectedPath[1024] = {0}, baseDirectory[1024] = {0};
    wchar_t relativePathWide[1024] = {0}, titleWide[1200] = {0};
    char titleUtf8[1200] = {0};
    const wchar_t *filter;
    if (relativePath == NULL || relativePathSize == 0 ||
        !ResolveBaseDirectory(request, baseDirectory, (int)(sizeof(baseDirectory) / sizeof(baseDirectory[0])))) return false;
    filter = request->filter != NULL ? request->filter : FILE_DIALOG_FILTER_ALL_FILES;
    snprintf(titleUtf8, sizeof(titleUtf8), "%s [Base: %s]",
             request->title != NULL ? request->title : "Select file", request->baseDirectory);
    (void)RpgFileIo_Utf8ToWide(titleUtf8, titleWide, (int)(sizeof(titleWide) / sizeof(titleWide[0])));
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = (DWORD)(sizeof(selectedPath) / sizeof(selectedPath[0]));
    dialog.lpstrInitialDir = baseDirectory;
    dialog.lpstrTitle = titleWide[0] != L'\0' ? titleWide : NULL;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&dialog) == FALSE ||
        !PathRelativePathToW(relativePathWide, baseDirectory, FILE_ATTRIBUTE_DIRECTORY,
                             selectedPath, FILE_ATTRIBUTE_NORMAL)) return false;
    if (wcsncmp(relativePathWide, L".\\", 2) == 0)
        memmove(relativePathWide, relativePathWide + 2,
                (wcslen(relativePathWide + 2) + 1) * sizeof(wchar_t));
    return RpgFileIo_WideToUtf8(relativePathWide, relativePath, (int)relativePathSize);
}

#if 0 /* Legacy entry point retained only as disabled source history. */
bool FileDialog_SelectFile(char *destinationPath, size_t destinationPathSize,
                           const char *initialDirectory)
{
    if (destinationPathSize == 0) return false;
    wchar_t selectedPath[1024] = {0};
    wchar_t initialDirectoryWide[1024] = {0};
    wchar_t resolvedDirectoryWide[1024] = {0};
    if (initialDirectory != NULL && initialDirectory[0] != '\0' &&
        !RpgFileIo_Utf8ToWide(initialDirectory, initialDirectoryWide,
                               (int)(sizeof(initialDirectoryWide) / sizeof(initialDirectoryWide[0])))) return false;
    // 共通ダイアログが前回の場所へ戻らないよう、初期フォルダは絶対パスへ正規化して渡す。
    if (initialDirectoryWide[0] != L'\0' &&
        GetFullPathNameW(initialDirectoryWide,
                         sizeof(resolvedDirectoryWide) / sizeof(resolvedDirectoryWide[0]),
                         resolvedDirectoryWide, NULL) == 0) return false;
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"All files (*.*)\0*.*\0";
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = (DWORD)(sizeof(selectedPath) / sizeof(selectedPath[0]));
    dialog.lpstrInitialDir = resolvedDirectoryWide[0] != L'\0' ? resolvedDirectoryWide : NULL;
    // 選択画面を閉じても、ゲーム本体の現在ディレクトリは変更しない。
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) != FALSE &&
           RpgFileIo_WideToUtf8(selectedPath, destinationPath, (int)destinationPathSize);
}
#endif
// 役割: Windows のファイル選択ダイアログをエディターから利用できるようにする。
