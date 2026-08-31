// 依存: file_dialog.h
#include "file_dialog.h"

/* 依存関係を更新: 共有RPGモジュールの配置に合わせる。 */
#include "../Shered/Rpg/rpg_file_io.h"

#include <windows.h>
#include <commdlg.h>

bool FileDialog_SelectPng(char *destinationPath, size_t destinationPathSize)
{
    if (destinationPath == NULL || destinationPathSize == 0) return false;
    wchar_t selectedPath[1024] = {0};
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"PNG files (*.png)\0*.png\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = (DWORD)(sizeof(selectedPath) / sizeof(selectedPath[0]));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    return GetOpenFileNameW(&dialog) != FALSE &&
           RpgFileIo_WideToUtf8(selectedPath, destinationPath, (int)destinationPathSize);
}

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
// 役割: Windows のファイル選択ダイアログをエディターから利用できるようにする。
