// 依存する自プロジェクト内ファイル: rpg_layout.h
#include "rpg_layout.h"

#include <stdio.h>

RpgLayout RpgLayout_Default(void)
{
    return (RpgLayout){ .playerPosition = { 360.0f, 400.0f }, .npcPosition = { 620.0f, 400.0f },
                         .playerMoveSpeed = 180.0f };
}

bool RpgLayout_Load(const char *filePath, RpgLayout *layout)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) return false;
    int readCount = fscanf(file, "%f %f %f %f %f", &layout->playerPosition.x,
                           &layout->playerPosition.y, &layout->npcPosition.x,
                           &layout->npcPosition.y, &layout->playerMoveSpeed);
    fclose(file);
    if (readCount == 4) layout->playerMoveSpeed = 180.0f;
    return readCount >= 4;
}

bool RpgLayout_Save(const char *filePath, const RpgLayout *layout)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "%.1f %.1f %.1f %.1f %.1f\n", layout->playerPosition.x,
            layout->playerPosition.y, layout->npcPosition.x, layout->npcPosition.y,
            layout->playerMoveSpeed);
    return fclose(file) == 0;
}
