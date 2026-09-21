// 依存する自プロジェクト内ファイル: rpg_zipper.h, rpg_stage.h
#include "rpg_zipper.h"
#include "rpg_stage.h"

#include <stdio.h>
#include <string.h>

#include "raymath.h"

RpgZipper RpgZipper_Default(void)
{
    RpgZipper zipper = { 0 };
    /* Zipper はステージへ初期配置しない。接続時だけランタイムがプレイヤー位置へ同期する。 */
    zipper.character = RpgCharacter_Create((Vector2){ -RPG_STAGE_TILE_SIZE, -RPG_STAGE_TILE_SIZE }, ORANGE, BROWN);
    zipper.inspect = RpgInspect_Default("Zipper", "Nothing unusual here.");
    zipper.launchSpeed = 720.0f;
    zipper.returnSpeed = 180.0f;
    zipper.followSpeed = 180.0f;
    zipper.launchPreviewEnabled = false;
    RpgZipper_ClearHeldObject(&zipper);
    return zipper;
}

void RpgZipper_ClearHeldObject(RpgZipper *zipper)
{
    if (zipper == NULL) return;
    zipper->heldObject = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                                 .blockCell = { -1, -1 },
                                                 .movableKind = RPG_ZIPPER_MOVABLE_NONE, .dataShotIndex = -1,
                                                 .dynamicBlockIndex = -1, .referenceObjectIndex = -1 };
    zipper->returningObject = zipper->heldObject;
    memset(zipper->heldQueue, 0, sizeof(zipper->heldQueue));
    zipper->heldQueueCount = 0;
    zipper->isFolderReturnPending = false;
    zipper->isSpitCommandPending = false;
    zipper->isFolderReturnAnimating = false;
    zipper->isFolderReturnCommitPending = false;
    zipper->folderReturnDelayElapsed = 0.0f;
    zipper->folderReturnElapsed = 0.0f;
    zipper->folderReturnDuration = 0.45f;
}

bool RpgZipper_CanEnqueueHeldObject(const RpgZipper *zipper)
{
    return zipper != NULL && (zipper->heldObject.kind == RPG_ZIPPER_HELD_OBJECT_NONE ||
                              zipper->heldQueueCount < RPG_ZIPPER_HELD_QUEUE_CAPACITY);
}

bool RpgZipper_EnqueueHeldObject(RpgZipper *zipper, RpgZipperHeldObject object)
{
    if (zipper == NULL || object.kind == RPG_ZIPPER_HELD_OBJECT_NONE) return false;
    if (zipper->heldObject.kind == RPG_ZIPPER_HELD_OBJECT_NONE) {
        zipper->heldObject = object;
        return true;
    }
    if (zipper->heldQueueCount >= RPG_ZIPPER_HELD_QUEUE_CAPACITY) return false;
    zipper->heldQueue[zipper->heldQueueCount++] = object;
    return true;
}

void RpgZipper_RemoveNextHeldObject(RpgZipper *zipper)
{
    if (zipper == NULL) return;
    if (zipper->heldQueueCount <= 0) {
        zipper->heldObject = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                                     .blockCell = { -1, -1 },
                                                     .movableKind = RPG_ZIPPER_MOVABLE_NONE, .dataShotIndex = -1,
                                                     .dynamicBlockIndex = -1, .referenceObjectIndex = -1 };
        return;
    }
    zipper->heldObject = zipper->heldQueue[0];
    for (int index = 1; index < zipper->heldQueueCount; index++)
        zipper->heldQueue[index - 1] = zipper->heldQueue[index];
    zipper->heldQueue[--zipper->heldQueueCount] = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                                                            .blockCell = { -1, -1 },
                                                                            .movableKind = RPG_ZIPPER_MOVABLE_NONE,
                                                                            .dataShotIndex = -1,
                                                                            .dynamicBlockIndex = -1,
                                                                            .referenceObjectIndex = -1 };
}

bool RpgZipper_TryToggleLaunch(RpgZipper *zipper, RpgCharacter *player,
                               Vector2 aimWorldPosition, bool *followsPlayer,
                               bool *isLaunched, Vector2 *launchVelocity)
{
    if (zipper == NULL || player == NULL || followsPlayer == NULL ||
        isLaunched == NULL || launchVelocity == NULL) return false;
    if (*followsPlayer) {
        Rectangle bounds;
        Vector2 center;
        Vector2 direction;
        zipper->character.position = player->position;
        bounds = RpgCharacter_GetCollisionBounds(&zipper->character);
        center = (Vector2){ bounds.x + bounds.width * 0.5f,
                            bounds.y + bounds.height * 0.5f };
        direction = Vector2Subtract(aimWorldPosition, center);
        if (Vector2LengthSqr(direction) < 0.001f) direction = (Vector2){ 1.0f, 0.0f };
        *launchVelocity = Vector2Scale(Vector2Normalize(direction), zipper->launchSpeed);
        *isLaunched = true;
        *followsPlayer = false;
        RpgCharacter_ResetAnimation(player);
        return true;
    }
    if (*isLaunched) return false;
    *followsPlayer = true;
    return true;
}

// 旧設定は5項目、新設定は6項目。整数読込が小数部を別項目として扱わないよう、先に項目数を数える。
static int CountConfigValues(const char *line)
{
    int count = 0;
    bool inValue = false;
    for (const char *cursor = line; *cursor != '\0'; cursor++) {
        bool isSeparator = *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n';
        if (!isSeparator && !inValue) count++;
        inValue = !isSeparator;
    }
    return count;
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
    int readCount = 0;
    if (fgets(line, sizeof(line), file) != NULL) {
        int valueCount = CountConfigValues(line);
        if (valueCount == 7) {
            readCount = sscanf(line, "%f %f %f %f %d %f %f", &zipper->character.position.x,
                               &zipper->character.position.y, &zipper->character.scale,
                               &zipper->launchSpeed, &previewEnabled, &zipper->returnSpeed,
                               &zipper->followSpeed);
        } else if (valueCount == 6) {
            readCount = sscanf(line, "%f %f %f %f %d %f", &zipper->character.position.x,
                               &zipper->character.position.y, &zipper->character.scale,
                               &zipper->launchSpeed, &previewEnabled, &zipper->returnSpeed);
        } else if (valueCount == 5) {
            readCount = sscanf(line, "%f %f %f %d %f", &zipper->character.position.x,
                               &zipper->character.scale, &zipper->launchSpeed, &previewEnabled,
                               &zipper->returnSpeed);
        }
    }
    bool loaded = readCount >= 2;
    if (readCount >= 4) zipper->launchPreviewEnabled = previewEnabled != 0;
    fclose(file);
    if (zipper->character.position.y > RPG_STAGE_WORLD_HEIGHT) {
        RpgZipper defaults = RpgZipper_Default();
        zipper->character.position = defaults.character.position;
    }
    return loaded;
}

bool RpgZipper_Save(const char *filePath, const RpgZipper *zipper)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    bool saved = fprintf(file, "%.2f %.2f %.2f %.2f %d %.2f %.2f\n", zipper->character.position.x,
                         zipper->character.position.y, zipper->character.scale, zipper->launchSpeed,
                         zipper->launchPreviewEnabled ? 1 : 0, zipper->returnSpeed,
                         zipper->followSpeed) > 0;
    return fclose(file) == 0 && saved;
}

Rectangle RpgZipper_GetSpriteBounds(const RpgCharacter *character, float groundY)
{
    // Zipperの座標は主人公と同じ足元基準。射出中はY座標も反映する。
    (void)groundY;
    // 元画像の1フレームは32×40pxなので、48pxマスへ高さ基準で収めて縦横比を保つ。
    float height = RPG_STAGE_TILE_SIZE * Clamp(character->scale, 0.5f, 1.0f);
    float width = height * (32.0f / 40.0f);
    return (Rectangle){ character->position.x - width * 0.5f,
                        character->position.y - height, width, height };
}

Rectangle RpgZipper_GetPixelAlignedSpriteBounds(const RpgCharacter *character, float groundY)
{
    return RpgStage_SnapRenderRectangle(RpgZipper_GetSpriteBounds(character, groundY));
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
