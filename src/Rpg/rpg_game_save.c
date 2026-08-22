// 役割: Settings 配下の進行セーブファイルを読み書きする。
// 依存する自プロジェクト内ファイル: rpg_game_save.h
#include "rpg_game_save.h"

#include "raylib.h"

#include <stdio.h>

static bool GetSavePath(char *path, size_t pathSize)
{
    return snprintf(path, pathSize, "%s../assets/Settings/rpg_game_save.cfg", GetApplicationDirectory()) > 0;
}

RpgGameSave RpgGameSave_Default(void)
{
    return (RpgGameSave){ .isValid = false, .stageNumber = 1, .flagId = 0, .zipperConnected = false };
}

bool RpgGameSave_Load(RpgGameSave *save)
{
    char path[1200];
    FILE *file;
    int connected = 0;
    if (save == NULL || !GetSavePath(path, sizeof(path)) || (file = fopen(path, "rb")) == NULL) return false;
    *save = RpgGameSave_Default();
    if (fscanf(file, "v1 %d %d %d", &save->stageNumber, &save->flagId, &connected) != 3 ||
        save->stageNumber <= 0 || save->flagId <= 0) {
        fclose(file);
        *save = RpgGameSave_Default();
        return false;
    }
    fclose(file);
    save->zipperConnected = connected != 0;
    save->isValid = true;
    return true;
}

bool RpgGameSave_Save(const RpgGameSave *save)
{
    char path[1200];
    FILE *file;
    if (save == NULL || !save->isValid || save->stageNumber <= 0 || save->flagId <= 0 ||
        !GetSavePath(path, sizeof(path))) return false;
    file = fopen(path, "wb");
    if (file == NULL) return false;
    fprintf(file, "v1 %d %d %d\n", save->stageNumber, save->flagId, save->zipperConnected ? 1 : 0);
    return fclose(file) == 0;
}
