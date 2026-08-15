// 依存する自プロジェクト内ファイル: rpg_zipper.h, rpg_stage.h
#include "rpg_zipper.h"
#include "rpg_stage.h"

#include <stdio.h>
#include <string.h>

RpgZipper RpgZipper_Default(void)
{
    RpgZipper zipper = { 0 };
    zipper.character = RpgCharacter_Create((Vector2){ RPG_STAGE_WORLD_WIDTH - RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE / 2.0f, 400.0f }, ORANGE, BROWN);
    zipper.inspect = RpgInspect_Default("Zipper", "Nothing unusual here.");
    return zipper;
}

bool RpgZipper_Load(const char *filePath, RpgZipper *zipper)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) {
        // 専用設定が初めて作られるまでは、旧ステージイベント設定のZipper値を引き継ぐ。
        char legacyPath[512];
        char *fileName;
        snprintf(legacyPath, sizeof(legacyPath), "%s", filePath);
        fileName = strrchr(legacyPath, '/');
        if (fileName == NULL) fileName = strrchr(legacyPath, '\\');
        if (fileName == NULL) return false;
        strcpy(fileName + 1, "rpg_stage3_event.cfg");
        file = fopen(legacyPath, "r");
        if (file == NULL) return false;
        char header[128];
        int enabled, lineCount;
        bool loaded = fgets(header, sizeof(header), file) != NULL &&
                      sscanf(header, "v2 %d %f %f %d", &enabled, &zipper->character.position.x,
                             &zipper->character.scale, &lineCount) == 4;
        fclose(file);
        return loaded;
    }
    bool loaded = fscanf(file, "%f %f", &zipper->character.position.x, &zipper->character.scale) == 2;
    fclose(file);
    return loaded;
}

bool RpgZipper_Save(const char *filePath, const RpgZipper *zipper)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    bool saved = fprintf(file, "%.2f %.2f\n", zipper->character.position.x,
                         zipper->character.scale) > 0;
    return fclose(file) == 0 && saved;
}
