// 役割: UTF-8パスのWindows API変換と外部ファイルの一括読込を共通化する。
// 依存する自プロジェクト内ファイル: なし。
#ifndef RPG_FILE_IO_H
#define RPG_FILE_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#ifdef _WIN32
bool RpgFileIo_Utf8ToWide(const char *source, wchar_t *destination, int destinationCount);
bool RpgFileIo_WideToUtf8(const wchar_t *source, char *destination, int destinationCount);
#endif

bool RpgFileIo_ReadAllBytesUtf8(const char *path, size_t maximumSize,
                                unsigned char **bytes, int *byteCount);
void RpgFileIo_FreeBytes(unsigned char *bytes);

#endif
