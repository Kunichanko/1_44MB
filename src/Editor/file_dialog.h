// 依存: なし（Windows 共通ダイアログは外部 API）
#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <stdbool.h>
#include <stddef.h>

/* Win32 OPENFILENAME filter strings.  Callers may also supply their own
 * double-NUL-terminated filter to FileDialogRequest.filter. */
#define FILE_DIALOG_FILTER_PNG L"PNG files (*.png)\0*.png\0All files (*.*)\0*.*\0"
#define FILE_DIALOG_FILTER_ALL_FILES L"All files (*.*)\0*.*\0"

typedef struct FileDialogRequest {
    const char *baseDirectory;
    const wchar_t *filter;
    const char *title;
    /* Short label shown by the editor's shared picker UI. */
    const char *baseLabel;
} FileDialogRequest;

/* The selected value is returned relative to baseDirectory. */
bool FileDialog_Select(char *relativePath, size_t relativePathSize,
                       const FileDialogRequest *request);
bool FileDialog_ResolvePath(const FileDialogRequest *request, const char *relativePath,
                            char *absolutePath, size_t absolutePathSize);

#endif
// 役割: エディターから利用するファイル選択 API を宣言する。
