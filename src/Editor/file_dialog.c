// 依存: file_dialog.h
#include "file_dialog.h"

#include <windows.h>
#include <commdlg.h>

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
