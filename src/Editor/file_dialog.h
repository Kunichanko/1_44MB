// 依存: なし（Windows 共通ダイアログは外部 API）
#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include <stdbool.h>
#include <stddef.h>

bool FileDialog_SelectPng(char *destinationPath, size_t destinationPathSize);
/* Reference File用: 種類を限定せず、選択した実ファイルのUTF-8パスを返す。 */
bool FileDialog_SelectFile(char *destinationPath, size_t destinationPathSize,
                           const char *initialDirectory);

#endif
// 役割: エディターから利用するファイル選択 API を宣言する。
