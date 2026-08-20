// 依存する自プロジェクト内ファイル: rpg_layout.h
#include "rpg_layout.h"

#include <stdio.h>

RpgLayout RpgLayout_Default(void)
{
    return (RpgLayout){ .playerPosition = { 360.0f, 400.0f }, .npcPosition = { 620.0f, 400.0f },
                         .playerMoveSpeed = 180.0f, .playerScale = 1.0f, .npcScale = 1.0f,
                         .stage3IntroEnabled = true, .electricCellDelay = 0.15f };
}

bool RpgLayout_Load(const char *filePath, RpgLayout *layout)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) return false;
    int stage3IntroEnabled = 1;
    int readCount = fscanf(file, "%f %f %f %f %f %f %f %d %f", &layout->playerPosition.x,
                           &layout->playerPosition.y, &layout->npcPosition.x,
                           &layout->npcPosition.y, &layout->playerMoveSpeed, &layout->playerScale,
                           &layout->npcScale, &stage3IntroEnabled, &layout->electricCellDelay);
    fclose(file);
    if (readCount == 4) layout->playerMoveSpeed = 180.0f;
    if (readCount < 6) layout->playerScale = 1.0f;
    if (readCount < 7) layout->npcScale = 1.0f;
    if (readCount < 9) layout->electricCellDelay = 0.15f;
    if (layout->electricCellDelay < 0.01f || layout->electricCellDelay > 2.0f)
        layout->electricCellDelay = 0.15f;
    layout->stage3IntroEnabled = stage3IntroEnabled != 0;
    return readCount >= 4;
}

bool RpgLayout_Save(const char *filePath, const RpgLayout *layout)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "%.1f %.1f %.1f %.1f %.1f %.2f %.2f %d %.3f\n", layout->playerPosition.x,
            layout->playerPosition.y, layout->npcPosition.x, layout->npcPosition.y,
            layout->playerMoveSpeed, layout->playerScale, layout->npcScale,
            layout->stage3IntroEnabled ? 1 : 0, layout->electricCellDelay);
    return fclose(file) == 0;
}
// 役割: RPG のキャラクター配置と全体レイアウト設定を保存・読み込みする。
