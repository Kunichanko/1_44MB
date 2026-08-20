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
    zipper.launchSpeed = 720.0f;
    zipper.returnSpeed = 180.0f;
    zipper.launchPreviewEnabled = false;
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
    char line[128];
    int previewEnabled = 0;
    int readCount = fgets(line, sizeof(line), file) != NULL ?
                    sscanf(line, "%f %f %f %d %f", &zipper->character.position.x,
                           &zipper->character.scale, &zipper->launchSpeed, &previewEnabled,
                           &zipper->returnSpeed) : 0;
    bool loaded = readCount >= 2;
    if (readCount >= 4) zipper->launchPreviewEnabled = previewEnabled != 0;
    fclose(file);
    return loaded;
}

bool RpgZipper_Save(const char *filePath, const RpgZipper *zipper)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    bool saved = fprintf(file, "%.2f %.2f %.2f %d %.2f\n", zipper->character.position.x,
                         zipper->character.scale, zipper->launchSpeed,
                         zipper->launchPreviewEnabled ? 1 : 0, zipper->returnSpeed) > 0;
    return fclose(file) == 0 && saved;
}

Rectangle RpgZipper_GetSpriteBounds(const RpgCharacter *character, float groundY)
{
    // Zipperの座標は主人公と同じ足元基準。射出中はY座標も反映する。
    (void)groundY;
    return (Rectangle){ character->position.x - 24.0f * character->scale,
                        character->position.y - 20.0f - 60.0f * character->scale,
                        48.0f * character->scale, 60.0f * character->scale };
}

void RpgZipper_DrawPointerFeedback(Rectangle bounds, bool isHovered, bool isSelected)
{
    // Windowsの選択表示に合わせ、ホバーは淡色、クリック選択は濃い枠で段階を分ける。
    if (!isHovered && !isSelected) return;
    Rectangle highlight = { bounds.x - 4.0f, bounds.y - 4.0f, bounds.width + 8.0f, bounds.height + 8.0f };
    DrawRectangleRec(highlight, Fade(SKYBLUE, isSelected ? 0.34f : 0.16f));
    DrawRectangleLinesEx(highlight, isSelected ? 2.5f : 1.5f, isSelected ? BLUE : SKYBLUE);
}
// 役割: Zipper の設定、スプライト境界、クリック時の視覚フィードバックを管理する。
