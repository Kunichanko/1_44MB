// 依存: file_dialog.h
#include "file_dialog.h"

#include <windows.h>
#include <commdlg.h>

static bool Utf8ToWide(const char *source, wchar_t *destination, size_t destinationCount)
{
    if (source == NULL || destination == NULL || destinationCount == 0) return false;
    return MultiByteToWideChar(CP_UTF8, 0, source, -1, destination, (int)destinationCount) > 0;
}

static bool WideToUtf8(const wchar_t *source, char *destination, size_t destinationSize)
{
    if (source == NULL || destination == NULL || destinationSize == 0) return false;
    return WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, (int)destinationSize,
                               NULL, NULL) > 0;
}

bool FileDialog_SelectPng(char *destinationPath, size_t destinationPathSize)
{
    if (destinationPathSize == 0) {
        return false;
    }

    destinationPath[0] = '\0';
    OPENFILENAMEA dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "PNG files (*.png)\0*.png\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = destinationPath;
    dialog.nMaxFile = (DWORD)destinationPathSize;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    return GetOpenFileNameA(&dialog) != FALSE;
}

bool FileDialog_SelectText(char *destinationPath, size_t destinationPathSize,
                           const char *initialDirectory)
{
    if (destinationPathSize == 0) return false;
    wchar_t selectedPath[1024] = {0};
    wchar_t initialDirectoryWide[1024] = {0};
    wchar_t resolvedDirectoryWide[1024] = {0};
    if (initialDirectory != NULL && initialDirectory[0] != '\0' &&
        !Utf8ToWide(initialDirectory, initialDirectoryWide,
                    sizeof(initialDirectoryWide) / sizeof(initialDirectoryWide[0]))) return false;
    // 共通ダイアログが前回の場所へ戻らないよう、初期フォルダは絶対パスへ正規化して渡す。
    if (initialDirectoryWide[0] != L'\0' &&
        GetFullPathNameW(initialDirectoryWide,
                         sizeof(resolvedDirectoryWide) / sizeof(resolvedDirectoryWide[0]),
                         resolvedDirectoryWide, NULL) == 0) return false;
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = (DWORD)(sizeof(selectedPath) / sizeof(selectedPath[0]));
    dialog.lpstrInitialDir = resolvedDirectoryWide[0] != L'\0' ? resolvedDirectoryWide : NULL;
    // 選択画面を閉じても、ゲーム本体の現在ディレクトリは変更しない。
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) != FALSE && WideToUtf8(selectedPath, destinationPath, destinationPathSize);
}
// 役割: Windows のファイル選択ダイアログをエディターから利用できるようにする。
