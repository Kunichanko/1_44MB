// 依存: なし（Windows 共通ダイアログは外部 API）
#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <stdbool.h>
#include <stddef.h>

bool FileDialog_SelectPng(char *destinationPath, size_t destinationPathSize);
bool FileDialog_SelectText(char *destinationPath, size_t destinationPathSize,
                           const char *initialDirectory);

#endif
// 役割: エディターから利用するファイル選択 API を宣言する。
