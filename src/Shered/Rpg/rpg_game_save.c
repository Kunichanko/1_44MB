// 役割: Settings 配下の進行セーブファイルを読み書きする。
// 依存する自プロジェクト内ファイル: rpg_game_save.h
#include "rpg_game_save.h"

#include "raylib.h"

#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

/* 進行度は Settings ではなく、本編用 StageN の Player フォルダにだけ記録する。 */
static bool GetSavePath(int stageNumber, char *path, size_t pathSize)
{
    return stageNumber > 0 && snprintf(path, pathSize,
        "%sStage\\game\\Stage%d\\objects\\Player\\progress.cfg", GetApplicationDirectory(), stageNumber) > 0;
}

static bool EnsureSaveDirectory(int stageNumber)
{
#ifdef _WIN32
    char path[1200];
    wchar_t wide[1200];
    if (!GetSavePath(stageNumber, path, sizeof(path))) return false;
    for (char *cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor != '\\') continue;
        *cursor = '\0';
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1200) <= 0 ||
            (CreateDirectoryW(wide, NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS)) {
            *cursor = '\\';
            return false;
        }
        *cursor = '\\';
    }
    return true;
#else
    (void)stageNumber;
    return false;
#endif
}

static bool HasZipperStructure(int stageNumber)
{
#ifdef _WIN32
    char path[1200];
    wchar_t wide[1200];
    DWORD attributes;
    if (snprintf(path, sizeof(path), "%sStage\\game\\Stage%d\\folders\\Zipper",
                 GetApplicationDirectory(), stageNumber) <= 0 ||
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, 1200) <= 0) return false;
    attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    (void)stageNumber;
    return false;
#endif
}

RpgGameSave RpgGameSave_Default(void)
{
    return (RpgGameSave){ .isValid = false, .stageNumber = 1, .flagId = 0, .zipperConnected = false };
}

bool RpgGameSave_Load(RpgGameSave *save)
{
    char path[1200];
    time_t newest = 0;
    if (save == NULL) return false;
    *save = RpgGameSave_Default();
    for (int stageNumber = 1; stageNumber <= 32; stageNumber++) {
        FILE *file;
        int storedStage = 0, flagId = 0, ignoredConnection = 0;
        time_t modified;
        if (!GetSavePath(stageNumber, path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) continue;
        bool valid = fscanf(file, "v2 %d %d %d", &storedStage, &flagId, &ignoredConnection) == 3 &&
                     storedStage == stageNumber && flagId > 0;
        fclose(file);
        modified = GetFileModTime(path);
        if (!valid || modified < newest) continue;
        newest = modified;
        save->stageNumber = stageNumber;
        save->flagId = flagId;
    }
    if (newest == 0) return false;
    /* 接続状態は古いフラグではなく、実在する Zipper 構造から毎回決める。 */
    save->zipperConnected = HasZipperStructure(save->stageNumber);
    return save->isValid = true;
}

bool RpgGameSave_Save(const RpgGameSave *save)
{
    char path[1200];
    FILE *file;
    if (save == NULL || !save->isValid || save->stageNumber <= 0 || save->flagId <= 0 ||
        !GetSavePath(save->stageNumber, path, sizeof(path)) || !EnsureSaveDirectory(save->stageNumber)) return false;
    file = fopen(path, "wb");
    if (file == NULL) return false;
    fprintf(file, "v2 %d %d %d\n", save->stageNumber, save->flagId, save->zipperConnected ? 1 : 0);
    return fclose(file) == 0;
}
