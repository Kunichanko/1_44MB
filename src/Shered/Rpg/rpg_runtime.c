// 依存する自プロジェクト内ファイル: rpg_runtime.h, game_font.h, rpg_block_inventory.h, rpg_object_folder.h, rpg_runtime_update.h, rpg_explorer_shell.h
// 役割: 本編とエディター内プレイで共通のRPGフレーム更新と描画を実装する。
// 依存関係を更新: rpg_viewport.h を追加した。
#include "rpg_runtime.h"
#include "rpg_viewport.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif
#include "game_font.h"
#include "rpg_block_inventory.h"
#include "rpg_object_folder.h"
#include "rpg_stage_build.h"
#include "rpg_explorer_launcher.h"
#include "rpg_explorer_shell.h"
#include "rpg_magnet.h"
#include "rpg_runtime_update.h"
#include "rpg_scene.h"
static const float zipperImportAnimationDuration = 0.60f;
static Texture2D folderReturnIcon = { 0 };
static bool hasLoadedFolderReturnIcon = false;
static const char *GetReferenceFileName(const char *filePath);

/* 依存先: rpg_stage の追従File、rpg_object_folder の実ファイル移動。
   役割: 追従FileをFolderへ渡す間だけ表示位置を管理し、描画とファイル操作を分離する。 */
typedef struct RpgReferenceFolderTransfer {
    RpgStage *stage;
    RpgReferenceObjects *objects;
    RpgReferenceTarget folderTarget;
    int followerIndex;
    char sourcePath[RPG_STAGE_REFERENCE_PATH_LENGTH];
    char destinationDirectory[RPG_STAGE_REFERENCE_PATH_LENGTH];
    Vector2 startPosition;
    Vector2 destinationPosition;
    float drawScale;
    float elapsed;
    bool isKeyDoorTransfer;
    int keyDoorRow;
    int keyDoorColumn;
    char failureMessage[RPG_KEY_DOOR_FAILURE_TEXT_LENGTH];
    bool active;
} RpgReferenceFolderTransfer;

static RpgReferenceFolderTransfer referenceFolderTransfer = {
    .folderTarget = { .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 },
    .followerIndex = -1
};
static const float referenceFolderTransferDuration = 0.42f;
static RpgPlayerPushState playerPushState = { .heldBlockIndex = -1 };

static int GetRuntimeMapIndex(const RpgStage *stage, Vector2 position, int fallbackMapIndex)
{
    int mapIndex = RpgStage_GetMapAtWorldPosition(stage, position);
    if (mapIndex >= 0) return mapIndex;
    if (RpgStage_IsMapActive(stage, fallbackMapIndex)) return fallbackMapIndex;
    return RpgStage_FindNearestActiveMapAtGrid(stage,
        (int)floorf(position.x / (RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE)),
        -(int)floorf(position.y / RPG_STAGE_WORLD_HEIGHT));
}

/* Storage keeps map cells in horizontal slots, while rendering places each slot at
   its persistent two-dimensional area coordinate. */
static Vector2 GetRuntimeMapRenderOffset(const RpgStage *stage, int mapIndex)
{
    (void)stage;
    (void)mapIndex;
    return (Vector2){ 0.0f, 0.0f };
}

/* Local map draw functions use x=0 for the first column of a map.  The saved
   stage remains horizontally packed, so this is intentionally separate from
   GetRuntimeMapRenderOffset(), which projects world-space objects. */
static Vector2 GetRuntimeMapRenderOrigin(const RpgStage *stage, int mapIndex)
{
    if (!RpgStage_IsMapActive(stage, mapIndex)) return (Vector2){ 0.0f, 0.0f };
    return (Vector2){
        (float)stage->mapGridX[mapIndex] * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE,
        -(float)stage->mapGridY[mapIndex] * RPG_STAGE_WORLD_HEIGHT
    };
}

static Vector2 StoragePositionToWorld(const RpgStage *stage, Vector2 position, int mapIndex)
{
    const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    const float mapHeight = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE;
    if (!RpgStage_IsMapActive(stage, mapIndex)) return position;
    return Vector2Add(position, (Vector2){
        (stage->mapGridX[mapIndex] - mapIndex) * mapWidth,
        -stage->mapGridY[mapIndex] * mapHeight
    });
}

static void InitializeRuntimeWorldCoordinates(RpgRuntimeContext *context)
{
    const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    if (context == NULL || context->stage == NULL || context->player == NULL ||
        context->worldCoordinatesInitialized == NULL || *context->worldCoordinatesInitialized) return;
    int playerMap = context->previousMap == NULL ? -1 : *context->previousMap;
    if (!RpgStage_IsMapActive(context->stage, playerMap))
        playerMap = (int)floorf(context->player->position.x / mapWidth);
    context->player->position = StoragePositionToWorld(context->stage, context->player->position, playerMap);
    if (context->zipper != NULL && context->zipper->character.position.x >= 0.0f) {
        int mapIndex = (int)floorf(context->zipper->character.position.x / mapWidth);
        context->zipper->character.position = StoragePositionToWorld(context->stage,
            context->zipper->character.position, mapIndex);
    }
    if (context->npc != NULL && context->npc->position.x >= 0.0f) {
        int mapIndex = (int)floorf(context->npc->position.x / mapWidth);
        context->npc->position = StoragePositionToWorld(context->stage, context->npc->position, mapIndex);
    }
    if (context->referenceDrops != NULL) for (int index = 0; index < context->referenceDrops->count; index++) {
        RpgReferenceObject *object = &context->referenceDrops->entries[index];
        int mapIndex = (int)floorf(object->position.x / mapWidth);
        object->position = StoragePositionToWorld(context->stage, object->position, mapIndex);
    }
    if (context->previousMap != NULL)
        *context->previousMap = RpgStage_GetMapAtWorldPosition(context->stage, context->player->position);
    *context->worldCoordinatesInitialized = true;
}

static void DrawConnectedStageMaps(const RpgStage *stage, const RpgMagnetRuntime *magnetRuntime,
                                   const RpgWires *wires, const RpgReceivers *receivers,
                                   const RpgAttachments *attachments, const RpgDataShots *dataShots,
                                   Texture2D fileTexture, const RpgStageBackground *stageBackground,
                                   float backgroundBrightness, float blockBrightness)
{
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        if (!RpgStage_IsMapActive(stage, mapIndex)) continue;

        int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
        Vector2 origin = GetRuntimeMapRenderOrigin(stage, mapIndex);
        rlPushMatrix();
        rlTranslatef(origin.x, origin.y, 0.0f);

        RpgStageBackground_Draw(stageBackground,
                                (Rectangle){ 0.0f, 0.0f,
                                             (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                             (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) },
                                backgroundBrightness);
        RpgImageObjects_DrawLayer(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                                  RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_BACK);
        RpgStage_DrawMap(stage, mapIndex, false, blockBrightness);

        for (int row = 0; row < RPG_STAGE_ROWS; row++) {
            for (int localColumn = 0; localColumn < RPG_STAGE_COLUMNS; localColumn++) {
                int column = firstColumn + localColumn;
                RpgObjectFolder objectFolder = { .cell = { row, column } };
                if (!RpgObjectFolder_BlockHasLinkedFiles(&objectFolder, stage->blocks[row][column])) continue;
                Rectangle objectBounds = { localColumn * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
                DrawRectangleRec(objectBounds, Fade(GOLD, 0.30f));
                DrawRectangleLinesEx(objectBounds, 2.0f, ORANGE);
            }
        }

        RpgStage_DrawMapReferenceObjects(stage, mapIndex, fileTexture);
        RpgStage_DrawMapEffects(stage, mapIndex);
        rlPushMatrix();
        rlTranslatef(-(float)(firstColumn * RPG_STAGE_TILE_SIZE), 0.0f, 0.0f);
        RpgMagnets_DrawFields(stage, firstColumn, RPG_STAGE_COLUMNS);
        rlPopMatrix();
        RpgWires_DrawMap(wires, stage, mapIndex);
        RpgWires_DrawElectric(wires, dataShots, firstColumn, RPG_STAGE_COLUMNS);
        RpgReceivers_DrawMap(receivers, mapIndex);
        RpgAttachments_DrawMap(attachments, mapIndex);
        RpgDataShots_DrawMap(dataShots, mapIndex);
        RpgImageObjects_DrawLayer(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                                  RPG_STAGE_TILE_SIZE, WHITE,
                                  RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
        RpgImageObjects_DrawLayer(&stage->imageObjects, mapIndex, RPG_STAGE_COLUMNS,
                                  RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_FRONT);
        rlPopMatrix();
    }
    /* Dynamic metals already use connected-world coordinates.  Draw them once
       after the per-slot static maps, not through a storage-slot transform. */
    RpgMagnets_DrawMetals(magnetRuntime, 0, RPG_STAGE_WORLD_COLUMNS, 0.0f, blockBrightness);
}

/* Convert the camera's rendered world position back into the stage's local coordinates. */
static Vector2 GetRuntimePointerWorldPosition(Camera2D camera, const RpgStage *stage, int mapIndex)
{
    (void)stage;
    (void)mapIndex;
    return GetScreenToWorld2D(RpgViewport_GetMousePosition(), camera);
}

/* Player-following objects are stored in the same slot coordinate system as the
   player.  Area changes remap that storage coordinate, so move every follower
   by the exact remap delta before its normal smoothing update runs. */
static void TranslatePlayerFollowers(RpgRuntimeContext *context, Vector2 delta)
{
    if (context == NULL) return;
    if (context->zipperFollowsPlayer != NULL && *context->zipperFollowsPlayer && context->zipper != NULL)
        context->zipper->character.position = Vector2Add(context->zipper->character.position, delta);
    if (context->referenceDrops == NULL) return;
    for (int index = 0; index < context->referenceDrops->count; index++) {
        RpgReferenceObject *object = &context->referenceDrops->entries[index];
        if (object->followsPlayer) object->position = Vector2Add(object->position, delta);
    }
}

static bool MovePlayerToAdjacentArea(RpgRuntimeContext *context, int *mapIndex,
                                     RpgAreaDirection direction)
{
    const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    const float mapHeight = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE;
    int targetMap;
    float localX;
    float localY;
    Vector2 previousPosition;
    if (context == NULL || mapIndex == NULL || context->stage == NULL || context->player == NULL) return false;
    *mapIndex = RpgStage_FindNearestActiveMap(context->stage, *mapIndex);
    if (*mapIndex < 0) return false;
    targetMap = RpgStage_GetAdjacentMap(context->stage, *mapIndex, direction);
    if (targetMap < 0) return false;
    previousPosition = context->player->position;
    localX = context->player->position.x - context->stage->mapGridX[*mapIndex] * mapWidth;
    localY = context->player->position.y + context->stage->mapGridY[*mapIndex] * mapHeight;
    context->player->position.x = context->stage->mapGridX[targetMap] * mapWidth + localX;
    context->player->position.y = -context->stage->mapGridY[targetMap] * mapHeight + localY;
    Vector2 transitionDelta = Vector2Subtract(context->player->position, previousPosition);
    TranslatePlayerFollowers(context, transitionDelta);
    RpgMagnets_TranslateHeldPushBlock(context->magnetRuntime, &playerPushState, transitionDelta);
    *mapIndex = targetMap;
    return true;
}

static bool GetAreaMoveDirectionFromArrowKey(RpgAreaDirection *direction)
{
    if (direction == NULL) return false;
    if (IsKeyPressed(KEY_LEFT)) *direction = RPG_AREA_LEFT;
    else if (IsKeyPressed(KEY_RIGHT)) *direction = RPG_AREA_RIGHT;
    else if (IsKeyPressed(KEY_UP)) *direction = RPG_AREA_UP;
    else if (IsKeyPressed(KEY_DOWN)) *direction = RPG_AREA_DOWN;
    else return false;
    return true;
}

static void TransitionPlayerBetweenAreas(RpgRuntimeContext *context, int *mapIndex)
{
    const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    const float mapHeight = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE;
    RpgCharacter *player;
    int direction = -1;
    int targetMap;
    float mapLeft;
    float mapTop;
    Vector2 previousPosition;
    Vector2 transitionDelta;
    if (context == NULL || mapIndex == NULL || context->stage == NULL || context->player == NULL) return;
    *mapIndex = RpgStage_FindNearestActiveMap(context->stage, *mapIndex);
    if (*mapIndex < 0) return;
    player = context->player;
    previousPosition = player->position;
    mapLeft = context->stage->mapGridX[*mapIndex] * mapWidth;
    mapTop = -context->stage->mapGridY[*mapIndex] * mapHeight;
    if (player->position.x < mapLeft) direction = RPG_AREA_LEFT;
    else if (player->position.x >= mapLeft + mapWidth) direction = RPG_AREA_RIGHT;
    else if (player->position.y < mapTop) direction = RPG_AREA_UP;
    else if (player->position.y >= mapTop + mapHeight) direction = RPG_AREA_DOWN;
    if (direction < 0) return;
    targetMap = RpgStage_GetAdjacentMap(context->stage, *mapIndex, (RpgAreaDirection)direction);
    if (targetMap < 0) {
        if (direction == RPG_AREA_LEFT) player->position.x = mapLeft;
        else if (direction == RPG_AREA_RIGHT) player->position.x = mapLeft + mapWidth - 0.01f;
        else if (direction == RPG_AREA_UP) player->position.y = mapTop;
        else player->position.y = mapTop + mapHeight - 0.01f;
        player->verticalSpeed = 0.0f;
        return;
    }
    /* Areas are one connected world.  Only validate the destination border;
       positions and followers must not be remapped through storage slots. */
    if (RpgStage_CheckSolidCollision(context->stage, RpgCharacter_GetCollisionBounds(player))) {
        player->position = previousPosition;
        if (direction == RPG_AREA_LEFT) player->position.x = mapLeft + 0.01f;
        else if (direction == RPG_AREA_RIGHT) player->position.x = mapLeft + mapWidth - 0.01f;
        else if (direction == RPG_AREA_UP) player->position.y = mapTop + 0.01f;
        else player->position.y = mapTop + mapHeight - 0.01f;
        player->verticalSpeed = 0.0f;
        return;
    }
    (void)transitionDelta;
    *mapIndex = targetMap;
}

typedef struct RpgNearbyKeyDoor {
    int row;
    int column;
} RpgNearbyKeyDoor;

void RpgRuntime_ResetTransientState(void)
{
    referenceFolderTransfer = (RpgReferenceFolderTransfer){
        .folderTarget = { .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 },
        .followerIndex = -1
    };
    playerPushState = RpgPlayerPushState_Default();
}

static const char *npcTalkPrompt = u8"[E] \u8a71\u3057\u304b\u3051\u308b";
static const Rectangle referenceTextCloseButton = { 648.0f, 262.0f, 144.0f, 26.0f };

static bool IsReferenceFolderTarget(const RpgStage *stage, RpgReferenceTarget target)
{
    return stage != NULL && target.kind == RPG_REFERENCE_TARGET_CELL && target.row >= 0 &&
           target.row < RPG_STAGE_ROWS && target.column >= 0 && target.column < RPG_STAGE_WORLD_COLUMNS &&
           RpgBlockInventory_IsReferenceFolder(stage->blocks[target.row][target.column]);
}

static RpgNearbyKeyDoor FindNearbyKeyDoor(const RpgStage *stage, Vector2 position, float maximumDistance)
{
    RpgNearbyKeyDoor result = { .row = -1, .column = -1 };
    float closestDistance = maximumDistance;
    if (stage == NULL) return result;
    for (int index = 0; index < stage->keyDoorCount; index++) {
        const RpgKeyDoor *door = &stage->keyDoors[index];
        if (door->rootRow < 0 || door->rootRow >= RPG_STAGE_ROWS || door->rootColumn < 0 ||
            door->rootColumn >= RPG_STAGE_WORLD_COLUMNS ||
            RpgBlockInventory_IsKeyDoorOpen(stage->blocks[door->rootRow][door->rootColumn])) continue;
        Vector2 center = RpgStage_GetWorldPositionForCell(stage, door->rootRow, door->rootColumn);
        center.y += RPG_STAGE_TILE_SIZE;
        float distance = Vector2Distance(position, center);
        if (distance <= closestDistance) {
            closestDistance = distance;
            result.row = door->rootRow;
            result.column = door->rootColumn;
        }
    }
    return result;
}

/* 弧を描く二次ベジェ補間で、FileがFolderへ収まる動きを作る。 */
static Vector2 GetReferenceFolderTransferPosition(void)
{
    float progress;
    Vector2 control;
    float inverse;
    if (!referenceFolderTransfer.active) return (Vector2){ 0.0f, 0.0f };
    progress = Clamp(referenceFolderTransfer.elapsed / referenceFolderTransferDuration, 0.0f, 1.0f);
    control = (Vector2){ (referenceFolderTransfer.startPosition.x + referenceFolderTransfer.destinationPosition.x) * 0.5f,
                         fminf(referenceFolderTransfer.startPosition.y,
                               referenceFolderTransfer.destinationPosition.y) - RPG_STAGE_TILE_SIZE * 1.5f };
    inverse = 1.0f - progress;
    return Vector2Add(Vector2Add(Vector2Scale(referenceFolderTransfer.startPosition, inverse * inverse),
                                 Vector2Scale(control, 2.0f * inverse * progress)),
                      Vector2Scale(referenceFolderTransfer.destinationPosition, progress * progress));
}

static bool StartReferenceFolderTransfer(RpgRuntimeContext *context, RpgReferenceTarget folderTarget)
{
    int followerIndex;
    RpgReferenceObject *follower;
    const char *destinationDirectory;
    if (context == NULL || referenceFolderTransfer.active || !IsReferenceFolderTarget(context->stage, folderTarget))
        return false;
    followerIndex = RpgReferenceObjects_FindFollowerIndex(context->referenceDrops);
    if (followerIndex < 0 || folderTarget.row < 0 || folderTarget.column < 0) return false;
    follower = &context->referenceDrops->entries[followerIndex];
    destinationDirectory = RpgReferenceObjects_GetTargetPath(context->stage, context->referenceDrops, folderTarget);
    if (follower->path[0] == '\0' || destinationDirectory[0] == '\0') return false;
    referenceFolderTransfer = (RpgReferenceFolderTransfer){
        .stage = context->stage,
        .objects = context->referenceDrops,
        .folderTarget = folderTarget,
        .followerIndex = followerIndex,
        .sourcePath = { 0 },
        .destinationDirectory = { 0 },
        .startPosition = follower->position,
        .destinationPosition = { (folderTarget.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                 (folderTarget.row + 0.5f) * RPG_STAGE_TILE_SIZE },
        .drawScale = follower->drawScale > 0.0f ? follower->drawScale : 1.0f,
        .elapsed = 0.0f,
        .active = true
    };
    /* 演出中にフォルダや配列の状態が変わっても、開始時に選んだ入出力先だけを使う。 */
    snprintf(referenceFolderTransfer.sourcePath, sizeof(referenceFolderTransfer.sourcePath), "%s", follower->path);
    snprintf(referenceFolderTransfer.destinationDirectory, sizeof(referenceFolderTransfer.destinationDirectory), "%s",
             destinationDirectory);
    /* アニメーション表示をこのモジュールへ一本化し、通常の追従更新から切り離す。 */
    follower->followsPlayer = false;
    return true;
}

static bool ReferenceFileMatchesKeyDoor(const RpgReferenceObject *file, const RpgKeyDoor *door)
{
    if (file == NULL || door == NULL || file->path[0] == '\0' || door->keyPath[0] == '\0') return false;
    return strcmp(GetReferenceFileName(file->path), GetReferenceFileName(door->keyPath)) == 0;
}

static bool StartKeyDoorTransfer(RpgRuntimeContext *context, RpgNearbyKeyDoor target)
{
    RpgKeyDoor *door;
    RpgReferenceObject *follower;
    int followerIndex;
    RpgObjectFolder folder;
    char destination[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
    if (context == NULL || referenceFolderTransfer.active || target.row < 0 || target.column < 0) return false;
    door = RpgStage_GetKeyDoorAtCell(context->stage, target.row, target.column);
    followerIndex = RpgReferenceObjects_FindFollowerIndex(context->referenceDrops);
    if (door == NULL || followerIndex < 0) return false;
    follower = &context->referenceDrops->entries[followerIndex];
    folder.cell = (RpgGridCell){ .row = door->rootRow, .column = door->rootColumn };
    if (!RpgObjectFolder_GetBlockDirectory(&folder, context->stage->blocks[door->rootRow][door->rootColumn],
                                           destination, sizeof(destination))) return false;
    referenceFolderTransfer = (RpgReferenceFolderTransfer){
        .stage = context->stage,
        .objects = context->referenceDrops,
        .folderTarget = { .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 },
        .followerIndex = followerIndex,
        .startPosition = follower->position,
        .destinationPosition = RpgStage_GetWorldPositionForCell(context->stage,
                                                                 door->rootRow, door->rootColumn),
        .drawScale = follower->drawScale > 0.0f ? follower->drawScale : 1.0f,
        .elapsed = 0.0f,
        .isKeyDoorTransfer = true,
        .keyDoorRow = door->rootRow,
        .keyDoorColumn = door->rootColumn,
        .active = true
    };
    referenceFolderTransfer.destinationPosition.y += RPG_STAGE_TILE_SIZE;
    snprintf(referenceFolderTransfer.sourcePath, sizeof(referenceFolderTransfer.sourcePath), "%s", follower->path);
    snprintf(referenceFolderTransfer.destinationDirectory, sizeof(referenceFolderTransfer.destinationDirectory), "%s", destination);
    snprintf(referenceFolderTransfer.failureMessage, sizeof(referenceFolderTransfer.failureMessage), "%s",
             door->failureText[0] != '\0' ? door->failureText : "This door needs its key file.");
    follower->followsPlayer = false;
    return true;
}

static int FindReferenceDropByPath(const RpgReferenceObjects *objects, const char *path)
{
    if (objects == NULL || path == NULL || path[0] == '\0') return -1;
    for (int index = 0; index < objects->count; index++)
        if (strcmp(objects->entries[index].path, path) == 0) return index;
    return -1;
}

/* P格納とZipperへのドラッグ格納で、実ファイル移動とゲーム上の削除を必ず同じ順序で行う。 */
static bool StoreReferenceTargetInDirectory(RpgRuntimeContext *context, RpgReferenceTarget sourceTarget,
                                            const char *destinationDirectory, const char *successMessage)
{
    const char *sourcePath;
    if (context == NULL || destinationDirectory == NULL || destinationDirectory[0] == '\0') return false;
    sourcePath = RpgReferenceObjects_GetTargetPath(context->stage, context->referenceDrops, sourceTarget);
    if (sourcePath == NULL || sourcePath[0] == '\0' ||
        !RpgObjectFolder_StoreFileInDirectory(sourcePath, destinationDirectory)) return false;
    RpgReferenceObjects_RemoveTarget(context->stage, context->referenceDrops, sourceTarget);
    snprintf(context->itemMessage, (size_t)context->itemMessageSize, "%s", successMessage);
    GameFont_AddText(context->itemMessage);
    *context->itemMessageTimer = 2.0f;
    return true;
}

static void UpdateReferenceFolderTransfer(RpgRuntimeContext *context, float deltaTime)
{
    RpgReferenceFolderTransfer *transfer = &referenceFolderTransfer;
    RpgReferenceTarget fileTarget;
    int followerIndex;
    if (!transfer->active) return;
    if (context == NULL || context->stage != transfer->stage || context->referenceDrops != transfer->objects) {
        transfer->active = false;
        transfer->followerIndex = -1;
        return;
    }
    transfer->elapsed += deltaTime;
    if (transfer->elapsed < referenceFolderTransferDuration) return;
    followerIndex = FindReferenceDropByPath(transfer->objects, transfer->sourcePath);
    if (followerIndex < 0) {
        transfer->active = false;
        transfer->followerIndex = -1;
        return;
    }
    fileTarget = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_DROP, .row = -1, .column = -1,
                                       .dropIndex = followerIndex };
    if (transfer->isKeyDoorTransfer && !ReferenceFileMatchesKeyDoor(&transfer->objects->entries[followerIndex],
                                                                     RpgStage_GetKeyDoorAtCell(context->stage,
                                                                                               transfer->keyDoorRow,
                                                                                               transfer->keyDoorColumn))) {
        transfer->objects->entries[followerIndex].followsPlayer = true;
        transfer->objects->entries[followerIndex].isCompressed = true;
        snprintf(context->itemMessage, (size_t)context->itemMessageSize, "%s", transfer->failureMessage);
        GameFont_AddText(context->itemMessage);
        *context->itemMessageTimer = 2.0f;
    } else if (StoreReferenceTargetInDirectory(context, fileTarget, transfer->destinationDirectory,
                                                transfer->isKeyDoorTransfer ? "Key accepted: door opened" : "File stored in folder")) {
        if (transfer->isKeyDoorTransfer)
            RpgStage_SetDoorOpenAtCell(context->stage, transfer->keyDoorRow, transfer->keyDoorColumn, true);
    } else {
        /* 実ファイル操作が失敗してもFileを失わない。追従へ復帰させて再試行可能にする。 */
        transfer->objects->entries[followerIndex].followsPlayer = true;
        snprintf(context->itemMessage, (size_t)context->itemMessageSize, "Could not store File");
        GameFont_AddText(context->itemMessage);
        *context->itemMessageTimer = 2.0f;
    }
    transfer->active = false;
    transfer->followerIndex = -1;
}

static int GetReferenceFolderTransferExcludedIndex(const RpgReferenceObjects *objects)
{
    if (!referenceFolderTransfer.active || referenceFolderTransfer.objects != objects) return -1;
    return referenceFolderTransfer.followerIndex;
}

static bool LoadReferenceText(const char *filePath, char *text, size_t textSize)
{
    FILE *file = NULL;
#ifdef _WIN32
    wchar_t widePath[RPG_STAGE_REFERENCE_PATH_LENGTH];
    if (MultiByteToWideChar(CP_UTF8, 0, filePath, -1, widePath,
                            RPG_STAGE_REFERENCE_PATH_LENGTH) > 0)
        file = _wfopen(widePath, L"rb");
#else
    file = fopen(filePath, "rb");
#endif
    if (file == NULL || textSize == 0) return false;
    size_t readSize = fread(text, 1, textSize - 1, file);
    text[readSize] = '\0';
    fclose(file);
    return true;
}

static const char *GetReferenceFileName(const char *filePath)
{
    const char *backslash = strrchr(filePath, '\\');
    const char *slash = strrchr(filePath, '/');
    const char *fileName = backslash != NULL ? backslash + 1 : filePath;
    if (slash != NULL && slash + 1 > fileName) fileName = slash + 1;
    return fileName[0] != '\0' ? fileName : "FILE.txt";
}

/* ゲーム内表示は安全に読めるテキスト形式だけに限定し、画像・実行ファイルなどのバイト列は描画しない。 */
static bool IsTextReferenceFile(const char *filePath)
{
    const char *extension = strrchr(GetReferenceFileName(filePath), '.');
    static const char *textExtensions[] = { ".txt", ".md", ".csv", ".json", ".log", ".cfg", ".ini" };
    if (extension == NULL) return false;
    for (int index = 0; index < (int)(sizeof(textExtensions) / sizeof(textExtensions[0])); index++) {
        const char *left = extension;
        const char *right = textExtensions[index];
        while (*left != '\0' && *right != '\0') {
            char character = *left;
            if (character >= 'A' && character <= 'Z') character = (char)(character - 'A' + 'a');
            if (character != *right) break;
            left++; right++;
        }
        if (*left == '\0' && *right == '\0') return true;
    }
    return false;
}

static void RegisterReferenceFileNames(const RpgStage *stage)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (RpgBlockInventory_IsReferenceObject(stage->blocks[row][column]))
            GameFont_AddText(GetReferenceFileName(RpgStage_GetReferencePathAtCell(stage, row, column)));
    }
}

static void OpenTextFile(const char *path, char *fileName, size_t fileNameSize,
                         char *text, size_t textSize, bool *isOpen)
{
    snprintf(fileName, fileNameSize, "%s", GetReferenceFileName(path));
    if (!IsTextReferenceFile(path)) {
        snprintf(text, textSize, "This file type cannot be displayed in the game.");
    } else if (path[0] != '\0' && LoadReferenceText(path, text, textSize)) {
        GameFont_AddText(text);
    } else {
        snprintf(text, textSize, "Text file is not assigned or cannot be read.");
    }
    GameFont_AddText(fileName);
    *isOpen = true;
}

// File.png の配置元にかかわらず、選択表示とドラッグ表示を同じ矩形で扱う。
static Rectangle GetReferenceTargetBounds(const RpgStage *stage, const RpgReferenceObjects *objects,
                                          RpgReferenceTarget target)
{
    if (target.kind == RPG_REFERENCE_TARGET_CELL)
        return RpgStage_GetWorldBoundsForCell(stage, target.row, target.column);
    if (target.kind == RPG_REFERENCE_TARGET_DROP && target.dropIndex >= 0 && target.dropIndex < objects->count) {
        const RpgReferenceObject *object = &objects->entries[target.dropIndex];
        float size = 48.0f * (object->drawScale > 0.0f ? object->drawScale : 1.0f);
        return (Rectangle){ object->position.x - size * 0.5f, object->position.y - size * 0.5f,
                            size, size };
    }
    return (Rectangle){ 0 };
}

static void DrawReferenceTextPanel(const char *fileName, const char *text)
{
    Rectangle panel = { 150.0f, 110.0f, 660.0f, 190.0f };
    DrawRectangleRec(panel, Fade(RAYWHITE, 0.96f));
    DrawRectangleLinesEx(panel, 2.0f, DARKBLUE);
    GameFont_Draw(fileName, 172.0f, 122.0f, 20.0f, DARKBLUE);
    int textOffset = 0;
    for (int row = 0; row < 7 && text[textOffset] != '\0'; row++) {
        char line[256] = { 0 };
        int length = 0;
        while (text[textOffset] != '\0' && text[textOffset] != '\n' && length < (int)sizeof(line) - 1)
            line[length++] = text[textOffset++];
        if (text[textOffset] == '\n') textOffset++;
        GameFont_Draw(line, 172.0f, 154.0f + row * 20.0f, 18.0f, DARKGRAY);
    }
    DrawRectangleRec(referenceTextCloseButton, Fade(DARKBLUE, 0.9f));
    DrawRectangleLinesEx(referenceTextCloseButton, 1.0f, RAYWHITE);
    GameFont_Draw("E: 閉じる", 674.0f, 267.0f, 16.0f, RAYWHITE);
}

/* Zipperへの取り込み成功は、cmd経由・FILE.pngドラッグ経由を問わず同じアニメーションで示す。 */
static void StartZipperImportAnimation(float *animationElapsed)
{
    if (animationElapsed != NULL) *animationElapsed = 0.0f;
}

static Texture2D GetFolderReturnIcon(void)
{
    if (!hasLoadedFolderReturnIcon) {
        folderReturnIcon = RpgExplorerShell_LoadFolderIconTexture();
        hasLoadedFolderReturnIcon = true;
    }
    return folderReturnIcon;
}

static Vector2 GetHeldObjectReturnDestination(const RpgRuntimeContext *context)
{
    const RpgZipperHeldObject *held = &(*context->zipper).heldObject;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_DATA_SHOT && held->dataShotIndex >= 0 &&
        held->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT)
        return (*context->dataShots).entries[held->dataShotIndex].position;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_ATTACHMENT && held->attachmentIndex >= 0 &&
        held->attachmentIndex < (*context->attachments).count)
        return RpgAttachments_GetPosition(&(*context->attachments).entries[held->attachmentIndex], 0);
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK)
        return (Vector2){ (held->blockCell.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                          (held->blockCell.row + 0.5f) * RPG_STAGE_TILE_SIZE };
    return (*context->zipper).character.position;
}

static void StartZipperFolderReturnVisual(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    zipper->isFolderReturnAnimating = true;
    zipper->folderReturnDelayElapsed = context->layout != NULL ?
        Clamp((*context->layout).zipperFolderReturnAnimationDelay, 0.0f, 5.0f) : 0.0f;
    zipper->folderReturnElapsed = 0.0f;
    zipper->folderReturnDuration = context->layout != NULL ?
        Clamp((*context->layout).zipperFolderReturnDuration, 0.10f, 5.0f) : 0.45f;
    zipper->folderReturnStart = zipper->character.position;
    zipper->folderReturnDestination = GetHeldObjectReturnDestination(context);
}

/* 1マス占有アタッチメントのフォルダがInboxへ移動した間は、対応マスを赤い壁として扱う。 */
static void SetHeldAttachmentCellError(RpgStage *stage, RpgAttachment *attachment, bool isMissing)
{
    RpgGridCell occupiedCell;
    if (stage == NULL || attachment == NULL || !RpgAttachments_GetOccupiedCell(attachment, &occupiedCell)) return;
    if (isMissing) stage->blocks[occupiedCell.row][occupiedCell.column] = RPG_BLOCK_BUILD_MISSING;
    else if (stage->blocks[occupiedCell.row][occupiedCell.column] == RPG_BLOCK_BUILD_MISSING)
        stage->blocks[occupiedCell.row][occupiedCell.column] = 0;
}

/* 返却開始時は Inbox から StageN へ移すだけにする。演出中はフォルダを build に戻さない。 */
static bool BeginZipperHeldObjectReturn(RpgRuntimeContext *context)
{
    RpgZipperHeldObject *held = &(*context->zipper).heldObject;
    bool returned = false;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_NONE) return true;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_DATA_SHOT && held->dataShotIndex >= 0 &&
        held->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT)
        returned = RpgObjectFolder_BeginReturnDataShotFromZipper(&(*context->dataShots).entries[held->dataShotIndex]);
    else if (held->kind == RPG_ZIPPER_HELD_OBJECT_ATTACHMENT && held->attachmentIndex >= 0 &&
             held->attachmentIndex < (*context->attachments).count) {
        RpgAttachment *attachment = &(*context->attachments).entries[held->attachmentIndex];
        returned = RpgObjectFolder_BeginReturnAttachmentFromZipper(attachment);
    }
    else if (held->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK && held->blockCell.row >= 0 && held->blockCell.column >= 0) {
        RpgObjectFolder folder = { .cell = held->blockCell };
        returned = RpgObjectFolder_BeginReturnBlockFromZipper(&folder, held->blockType);
    }
    return returned;
}

/* 演出完了時にだけ StageN の待機フォルダを build へ確定し、対応するゲーム状態を復帰する。 */
static bool CompleteZipperHeldObjectReturn(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    RpgZipperHeldObject *held = &zipper->returningObject;
    bool returned = false;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_NONE) return true;
    if (held->kind == RPG_ZIPPER_HELD_OBJECT_DATA_SHOT && held->dataShotIndex >= 0 &&
        held->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT) {
        returned = RpgObjectFolder_ReturnDataShotFromZipper(&(*context->dataShots).entries[held->dataShotIndex]);
        if (returned) {
            RpgDataShot *shot = &(*context->dataShots).entries[held->dataShotIndex];
            (void)RpgObjectFolder_RestoreDataShotFromMetadata(shot);
            shot->isZipperHeld = false;
        }
    }
    else if (held->kind == RPG_ZIPPER_HELD_OBJECT_ATTACHMENT && held->attachmentIndex >= 0 &&
             held->attachmentIndex < (*context->attachments).count) {
        RpgAttachment *attachment = &(*context->attachments).entries[held->attachmentIndex];
        returned = RpgObjectFolder_ReturnAttachmentFromZipper(attachment);
        if (returned) {
            attachment->isZipperHeld = false;
            SetHeldAttachmentCellError(context->stage, attachment, false);
        }
    } else if (held->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK && held->blockCell.row >= 0 && held->blockCell.column >= 0) {
        RpgObjectFolder folder = { .cell = held->blockCell };
        returned = RpgObjectFolder_ReturnBlockFromZipper(&folder, held->blockType);
    }
    if (returned)
        *held = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                       .blockCell = { -1, -1 }, .attachmentIndex = -1, .dataShotIndex = -1 };
    if (returned) zipper->isFolderReturnCommitPending = false;
    return returned;
}

static bool CaptureCurrentZipperObject(RpgRuntimeContext *context)
{
    RpgZipperHeldObject held = { .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                 .blockCell = { -1, -1 }, .attachmentIndex = -1, .dataShotIndex = -1 };
    bool captured = false;
    if ((*context->attachedDataShotIndex) >= 0 && (*context->attachedDataShotIndex) < RPG_DATA_SHOT_MAX_COUNT &&
        (*context->dataShots).entries[(*context->attachedDataShotIndex)].active) {
        captured = RpgObjectFolder_MoveDataShotToZipper(&(*context->dataShots).entries[(*context->attachedDataShotIndex)]);
        if (captured) (*context->dataShots).entries[(*context->attachedDataShotIndex)].isZipperHeld = true;
        held.kind = RPG_ZIPPER_HELD_OBJECT_DATA_SHOT;
        held.dataShotIndex = (*context->attachedDataShotIndex);
    } else if ((*context->isZipperAttachedToBlock) && (*context->zipperAttachedBlockCell).row >= 0 &&
               (*context->zipperAttachedBlockCell).column >= 0) {
        if ((*context->attachedAttachmentIndex) >= 0 && (*context->attachedAttachmentIndex) < (*context->attachments).count) {
            RpgAttachment *attachment = &(*context->attachments).entries[(*context->attachedAttachmentIndex)];
            captured = RpgObjectFolder_MoveAttachmentToZipper(attachment);
            if (captured) {
                attachment->isZipperHeld = true;
                SetHeldAttachmentCellError(context->stage, attachment, true);
            }
            held.kind = RPG_ZIPPER_HELD_OBJECT_ATTACHMENT;
            held.attachmentIndex = (*context->attachedAttachmentIndex);
        } else {
            RpgObjectFolder blockFolder = { .cell = (*context->zipperAttachedBlockCell) };
            held.kind = RPG_ZIPPER_HELD_OBJECT_BLOCK;
            held.blockCell = (*context->zipperAttachedBlockCell);
            held.blockType = (*context->stage).blocks[held.blockCell.row][held.blockCell.column];
            captured = RpgObjectFolder_MoveBlockToZipper(&blockFolder, held.blockType);
        }
    }
    if (!captured) return false;
    (*context->zipper).heldObject = held;
    (*context->zipperFollowsPlayer) = true;
    (*context->isZipperLaunched) = false;
    (*context->attachedDataShotIndex) = -1;
    (*context->attachedAttachmentIndex) = -1;
    (*context->isZipperAttachedToBlock) = false;
    (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
    return true;
}

/* cmdのゲーム機能。返却フォルダがない時、または返却演出が完了した時に次の対象を取得する。 */
static void CaptureZipperCommandTarget(RpgRuntimeContext *context)
{
    if (CaptureCurrentZipperObject(context)) return;
    /* 取得対象がなくても、cmd完了後のZipperは必ず帰還状態に統一する。 */
    (*context->zipperFollowsPlayer) = true;
    (*context->isZipperLaunched) = false;
    (*context->attachedDataShotIndex) = -1;
    (*context->attachedAttachmentIndex) = -1;
    (*context->isZipperAttachedToBlock) = false;
    (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
}

static void RunZipperCommandFunction(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    if (zipper->heldObject.kind != RPG_ZIPPER_HELD_OBJECT_NONE) {
        StartZipperFolderReturnVisual(context);
        if (BeginZipperHeldObjectReturn(context)) {
            /* 返却開始が完了すれば、演出中でも次の対象取得と追従復帰は待たせない。 */
            zipper->returningObject = zipper->heldObject;
            zipper->heldObject = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                                        .blockCell = { -1, -1 },
                                                        .attachmentIndex = -1, .dataShotIndex = -1 };
            zipper->isFolderReturnCommitPending = true;
            CaptureZipperCommandTarget(context);
        } else {
            zipper->isFolderReturnAnimating = false;
            zipper->folderReturnElapsed = 0.0f;
        }
        return;
    }
    CaptureZipperCommandTarget(context);
}

/* cmd起動時は演出だけを始め、ファイル操作はアニメーション終了まで待機する。 */
static void StartZipperCommand(RpgRuntimeContext *context)
{
    RpgZipper *zipper = context->zipper;
    StartZipperImportAnimation(context->zipperAnimationElapsed);
    (void)RpgObjectFolder_CompleteZipperCommandRequest();
    /* cmd入力と同時に旧フォルダを待機場所へ移し、演出後に次の取り込みを確定する。 */
    if (zipper->heldObject.kind != RPG_ZIPPER_HELD_OBJECT_NONE) {
        StartZipperFolderReturnVisual(context);
        if (BeginZipperHeldObjectReturn(context)) {
            zipper->returningObject = zipper->heldObject;
            zipper->heldObject = (RpgZipperHeldObject){ .kind = RPG_ZIPPER_HELD_OBJECT_NONE,
                                                        .blockCell = { -1, -1 },
                                                        .attachmentIndex = -1, .dataShotIndex = -1 };
            zipper->isFolderReturnCommitPending = true;
        } else {
            zipper->isFolderReturnAnimating = false;
            zipper->folderReturnElapsed = 0.0f;
        }
    }
    zipper->isFolderReturnPending = true;
}

void RpgRuntime_ProcessZipperCommand(RpgRuntimeContext *context)
{
    if (context == NULL || context->zipperAnimationElapsed == NULL ||
        !RpgObjectFolder_BeginZipperCommandRequest()) return;
    /* 実行中のcmdは一つだけにして、状態に関係なく同じ完了手順へ送る。 */
    if ((*context->zipper).isFolderReturnPending || (*context->zipper).isFolderReturnAnimating ||
        (*context->zipper).isFolderReturnCommitPending) {
        (void)RpgObjectFolder_CompleteZipperCommandRequest();
        return;
    }
    StartZipperCommand(context);
}

/* Folder内のCMD要求はbuild監視から一度だけ届く。同じエリアのFolderだけをZipper化し、
   既存の追従・射出処理へ状態を渡すことで、専用の追従処理を増やさない。 */
void RpgRuntime_ProcessReferenceFolderZipperCommand(RpgRuntimeContext *context)
{
    RpgGridCell folderCell;
    if (context == NULL || context->stage == NULL || context->player == NULL || context->zipper == NULL ||
        context->zipperFollowsPlayer == NULL || context->isZipperLaunched == NULL ||
        context->isZipperControllable == NULL || context->attachedDataShotIndex == NULL ||
        context->attachedAttachmentIndex == NULL || context->isZipperAttachedToBlock == NULL ||
        context->zipperAttachedBlockCell == NULL) return;
    if (!RpgStageBuild_ConsumeReferenceFolderZipperRequest(context->stage, context->player->position, &folderCell) ||
        !RpgObjectFolder_ActivateReferenceFolderAsZipper(context->stage, folderCell)) return;
    context->zipper->character.position = RpgStage_GetWorldPositionForCell(context->stage, folderCell.row, folderCell.column);
    context->zipper->character.position.y += RPG_STAGE_TILE_SIZE * 0.5f;
    context->zipper->character.verticalSpeed = 0.0f;
    context->zipper->character.isGrounded = true;
    *context->zipperFollowsPlayer = true;
    *context->isZipperLaunched = false;
    *context->isZipperControllable = true;
    *context->attachedDataShotIndex = -1;
    *context->attachedAttachmentIndex = -1;
    *context->isZipperAttachedToBlock = false;
    *context->zipperAttachedBlockCell = (RpgGridCell){ -1, -1 };
    /* マップ上の Folder 表示だけを Zipper 表示へ切り替える。実体は同じ Folder を Zipper 構造へ更新して保持する。 */
    context->stage->blocks[folderCell.row][folderCell.column] = 0;
    context->stage->referencePaths[folderCell.row][folderCell.column][0] = '\0';
    if (context->itemMessage != NULL && context->itemMessageSize > 0) {
        snprintf(context->itemMessage, (size_t)context->itemMessageSize, "Folder became Zipper");
        GameFont_AddText(context->itemMessage);
    }
    if (context->itemMessageTimer != NULL) *context->itemMessageTimer = 2.0f;
}

void RpgRuntime_UpdateZipperFolderReturn(RpgRuntimeContext *context, float deltaTime)
{
    RpgZipper *zipper;
    if (context == NULL || context->zipper == NULL || context->zipperAnimationElapsed == NULL) return;
    zipper = context->zipper;
    /* 取り込みアニメーション完了後、返却対象を StageN へ待機させて返却演出を始める。 */
    /* 返却フォルダの実移動・演出は独立しているため、取り込み演出が終われば待たずに帰還・次の取得へ進む。 */
    if (zipper->isFolderReturnPending && *context->zipperAnimationElapsed < 0.0f) {
        zipper->isFolderReturnPending = false;
        RunZipperCommandFunction(context);
    }
    if (!zipper->isFolderReturnAnimating) return;
    if (zipper->folderReturnDelayElapsed > 0.0f) {
        zipper->folderReturnDelayElapsed -= deltaTime;
        if (zipper->folderReturnDelayElapsed > 0.0f) return;
        zipper->folderReturnDelayElapsed = 0.0f;
    }
    zipper->folderReturnElapsed += deltaTime;
    if (zipper->folderReturnElapsed >= zipper->folderReturnDuration) {
        zipper->isFolderReturnAnimating = false;
        zipper->folderReturnElapsed = 0.0f;
        if (zipper->isFolderReturnCommitPending)
            (void)CompleteZipperHeldObjectReturn(context);
    }
}

void RpgRuntime_DrawZipperFolderReturn(const RpgZipper *zipper)
{
    Texture2D icon;
    float progress, eased, size;
    Vector2 position;
    Rectangle destination;
    if (zipper == NULL || !zipper->isFolderReturnAnimating || zipper->folderReturnDelayElapsed > 0.0f) return;
    progress = Clamp(zipper->folderReturnElapsed / Clamp(zipper->folderReturnDuration, 0.10f, 5.0f), 0.0f, 1.0f);
    eased = progress * progress * (3.0f - 2.0f * progress);
    position = Vector2Lerp(zipper->folderReturnStart, zipper->folderReturnDestination, eased);
    size = 24.0f - 7.0f * progress;
    destination = (Rectangle){ position.x - size * 0.5f, position.y - size * 0.5f, size, size };
    icon = GetFolderReturnIcon();
    if (icon.id != 0)
        DrawTexturePro(icon, (Rectangle){ 0, 0, (float)icon.width, (float)icon.height }, destination,
                       (Vector2){ 0, 0 }, 0.0f, Fade(WHITE, 1.0f - progress * 0.20f));
    else {
        DrawRectangleRounded(destination, 0.16f, 4, Fade(GOLD, 1.0f - progress * 0.20f));
        DrawRectangleLinesEx(destination, 1.0f, ORANGE);
    }
}

static void DrawZipper(Texture2D zipperTexture, const RpgCharacter *zipper, float animationElapsed)
{
    int frameCount = zipperTexture.width / 32;
    int frameIndex = animationElapsed >= 0.0f && frameCount > 1 ?
        (int)Clamp(animationElapsed / zipperImportAnimationDuration * frameCount, 0.0f, (float)(frameCount - 1)) : 0;
    Rectangle source = { frameIndex * 32.0f, 0.0f, 32.0f, 40.0f };
    Rectangle destination = RpgZipper_GetPixelAlignedSpriteBounds(zipper, 380.0f);
    DrawRectangleRounded((Rectangle){ destination.x - 3.0f, destination.y - 3.0f,
                                      destination.width + 6.0f, destination.height + 6.0f },
                         0.18f, 4, Fade(DARKBLUE, 0.28f));
    DrawRectangleLinesEx((Rectangle){ destination.x - 3.0f, destination.y - 3.0f,
                                      destination.width + 6.0f, destination.height + 6.0f },
                         1.0f, Fade(SKYBLUE, 0.85f));
    DrawTexturePro(zipperTexture, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

static void DrawMoveSprite(Texture2D zipperTexture, const RpgCharacter *player, const RpgCharacter *npc,
                           const RpgZipper *zipper, RpgInspectMoveTarget target, float x)
{
    if (target == RPG_INSPECT_MOVE_ZIPPER) {
        Rectangle source = { 0, 0, 32, 40 };
        Rectangle destination = { x - 24.0f * zipper->character.scale, 340.0f - 60.0f * zipper->character.scale,
                                  48.0f * zipper->character.scale, 60.0f * zipper->character.scale };
        DrawTexturePro(zipperTexture, source, destination, (Vector2){0}, 0, Fade(WHITE, 0.75f));
    } else {
        RpgCharacter sprite = target == RPG_INSPECT_MOVE_PLAYER ? *player : *npc;
        sprite.position = (Vector2){ x, 400.0f };
        if (target == RPG_INSPECT_MOVE_PLAYER) RpgCharacter_DrawPlayer(&sprite, RPG_CHARACTER_ANIMATION_IDLE);
        else RpgCharacter_Draw(&sprite, "");
    }
}

static float GetStageViewportZoom(void)
{
    /* 20x12 cells are 5:3, whereas the presentation viewport can be wider.
       Fit on both axes so editor full-map mode never crops its top/bottom row. */
    float widthZoom = (float)RpgViewport_GetWidth() /
                      (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
    float heightZoom = (float)RpgViewport_GetHeight() /
                       (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE);
    return fminf(widthZoom, heightZoom);
}

static void UpdateRpgCamera(Camera2D *camera, const RpgStage *stage, int mapIndex,
                            Vector2 playerPosition, bool followsPlayer)
{
    // 仮想表示の横幅から倍率を決め、本編の20x12とエディターの表示を同じカメラ処理で扱う。
    camera->zoom = GetStageViewportZoom();
    camera->offset = (Vector2){ RpgViewport_GetWidth() / 2.0f, RpgViewport_GetHeight() / 2.0f };
    if (followsPlayer) {
        /* Follow the player's position after the physical storage slot has been
           projected onto the two-dimensional area grid. */
        camera->target = Vector2Add(playerPosition, GetRuntimeMapRenderOffset(stage, mapIndex));
    } else {
        camera->target = Vector2Add(
            (Vector2){ stage->mapGridX[mapIndex] * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE +
                        RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE / 2.0f,
                        -stage->mapGridY[mapIndex] * RPG_STAGE_WORLD_HEIGHT +
                        RPG_STAGE_WORLD_HEIGHT / 2.0f },
            GetRuntimeMapRenderOffset(stage, mapIndex));
    }
}

static void UpdateZipperFollow(RpgZipper *zipper, const RpgCharacter *player, float deltaTime)
{
    // 帰還時は主人公の少し後ろ・同じ足元へ、X/Yをまとめて滑らかに追従させる。
    Vector2 target = { player->position.x - RPG_STAGE_TILE_SIZE * player->scale, player->position.y };
    Vector2 distance = Vector2Subtract(target, zipper->character.position);
    float maximumStep = zipper->followSpeed * deltaTime;
    float distanceLength = Vector2Length(distance);
    if (distanceLength <= maximumStep) zipper->character.position = target;
    else zipper->character.position = Vector2Add(zipper->character.position,
                                                  Vector2Scale(distance, maximumStep / distanceLength));
}

static Rectangle GetZipperCollisionBounds(const RpgZipper *zipper)
{
    return RpgCharacter_GetCollisionBounds(&zipper->character);
}

static Vector2 GetZipperCollisionCenter(const RpgZipper *zipper)
{
    Rectangle bounds = GetZipperCollisionBounds(zipper);
    return (Vector2){ bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };
}

static void MoveZipperCollisionCenterTo(RpgZipper *zipper, Vector2 targetCenter)
{
    // Zipperの座標は足元基準のため、当たり判定の中心を対象中心へ合わせて見た目と判定の上下ずれを防ぐ。
    Vector2 currentCenter = GetZipperCollisionCenter(zipper);
    zipper->character.position = Vector2Add(zipper->character.position,
                                             Vector2Subtract(targetCenter, currentCenter));
}

static bool GetStageCellAtCenter(const RpgStage *stage, Vector2 center, RpgGridCell *cell)
{
    int row;
    int column;
    if (!RpgStage_GetWorldCellAtPosition(stage, center, &row, &column)) return false;
    *cell = (RpgGridCell){ row, column };
    return true;
}

static Rectangle GetZipperForwardCollisionBounds(Rectangle bounds, Vector2 velocity)
{
    // 射出方向の前半面を判定面にし、背面で重なった物体には衝突・追従しないようにする。
    if (fabsf(velocity.x) >= fabsf(velocity.y)) {
        bounds.width *= 0.5f;
        if (velocity.x >= 0.0f) bounds.x += bounds.width;
    } else {
        bounds.height *= 0.5f;
        if (velocity.y >= 0.0f) bounds.y += bounds.height;
    }
    return bounds;
}

static bool DoesZipperHitReferenceFile(const RpgStage *stage, Rectangle bounds, Vector2 *center)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!RpgBlockInventory_IsReferenceObject(stage->blocks[row][column])) continue;
        Rectangle cell = RpgStage_GetWorldBoundsForCell(stage, row, column);
        if (CheckCollisionRecs(bounds, cell)) {
            *center = (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f };
            return true;
        }
    }
    return false;
}

static bool DoesZipperHitAttachment(const RpgStage *stage, const RpgAttachments *attachments, Rectangle bounds, Vector2 *center,
                                    RpgGridCell *attachmentCell, int *attachmentIndex)
{
    for (int index = 0; index < attachments->count; index++) {
        if (attachments->entries[index].isZipperHeld) continue;
        RpgGridCell outerCell = RpgGridPath_GetSideNeighbor(attachments->entries[index].cell,
                                                            attachments->entries[index].side);
        Vector2 position = RpgStage_GetWorldPositionForCell(stage, outerCell.row, outerCell.column);
        Rectangle attachmentBounds = { position.x - 22.0f, position.y - 22.0f, 44.0f, 44.0f };
        if (CheckCollisionRecs(bounds, attachmentBounds)) {
            *center = position;
            *attachmentCell = attachments->entries[index].cell;
            *attachmentIndex = index;
            return true;
        }
    }
    return false;
}

static int FindDataShotHit(const RpgDataShots *shots, Rectangle bounds)
{
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        const RpgDataShot *shot = &shots->entries[index];
        if (shot->active && CheckCollisionCircleRec(shot->position, shot->size, bounds)) return index;
    }
    return -1;
}

static void UpdateLaunchedZipper(RpgZipper *zipper, Vector2 *velocity, const RpgStage *stage,
                                 const RpgAttachments *attachments, const RpgDataShots *shots,
                                 float deltaTime, bool *isLaunched, int *attachedDataShotIndex,
                                 Vector2 *attachedDataShotOffset,
                                 bool *isAttachedToBlock, RpgGridCell *attachedBlockCell,
                                 int *attachedAttachmentIndex)
{
    // 高速設定でも壁をすり抜けないよう、移動を小さな単位に分けて衝突を確認する。
    float distance = Vector2Length(*velocity) * deltaTime;
    int stepCount = (int)ceilf(distance / 4.0f);
    if (stepCount < 1) stepCount = 1;
    Vector2 step = Vector2Scale(*velocity, deltaTime / (float)stepCount);
    for (int index = 0; index < stepCount; index++) {
        RpgZipper candidate = *zipper;
        candidate.character.position = Vector2Add(candidate.character.position, step);
        Rectangle collisionBounds = GetZipperCollisionBounds(&candidate);
        Rectangle forwardCollisionBounds = GetZipperForwardCollisionBounds(collisionBounds, *velocity);
        int dataShotIndex = FindDataShotHit(shots, forwardCollisionBounds);
        if (dataShotIndex >= 0) {
            // 動くデータ弾には相対位置を記録してくっつき、弾が消えるまで一緒に動かす。
            MoveZipperCollisionCenterTo(&candidate, shots->entries[dataShotIndex].position);
            *attachedDataShotIndex = dataShotIndex;
            *attachedDataShotOffset = Vector2Subtract(candidate.character.position,
                                                       shots->entries[dataShotIndex].position);
            *isLaunched = false;
            *isAttachedToBlock = false;
            *attachedAttachmentIndex = -1;
            *zipper = candidate;
            return;
        }
        Vector2 collisionCenter;
        RpgGridCell attachmentCell = { -1, -1 };
        int hitAttachmentIndex = -1;
        bool hitReferenceFile = DoesZipperHitReferenceFile(stage, forwardCollisionBounds, &collisionCenter);
        bool hitAttachment = !hitReferenceFile &&
            DoesZipperHitAttachment(stage, attachments, forwardCollisionBounds, &collisionCenter, &attachmentCell,
                                     &hitAttachmentIndex);
        if (hitReferenceFile || hitAttachment) {
            MoveZipperCollisionCenterTo(&candidate, collisionCenter);
            *isLaunched = false;
            *isAttachedToBlock = true;
            if (hitAttachment) {
                *attachedBlockCell = attachmentCell;
                *attachedAttachmentIndex = hitAttachmentIndex;
            } else {
                GetStageCellAtCenter(stage, collisionCenter, attachedBlockCell);
                *attachedAttachmentIndex = -1;
            }
            *zipper = candidate;
            return;
        }
        Vector2 forwardCenter = { forwardCollisionBounds.x + forwardCollisionBounds.width * 0.5f,
                                  forwardCollisionBounds.y + forwardCollisionBounds.height * 0.5f };
        bool hitWorldEdge = RpgStage_GetMapAtWorldPosition(stage, forwardCenter) < 0;
        if (hitWorldEdge || RpgStage_FindSolidCollisionCenter(stage, forwardCollisionBounds, &collisionCenter)) {
            if (!hitWorldEdge) {
                GetStageCellAtCenter(stage, collisionCenter, attachedBlockCell);
                /* 複数マスブロックでは、ドラッグと共通の代表マスへZipperを吸着させる。 */
                int rootRow;
                int rootColumn;
                if (RpgStage_FindEffectRootCell(stage, attachedBlockCell->row, attachedBlockCell->column,
                                                &rootRow, &rootColumn, NULL)) {
                    *attachedBlockCell = (RpgGridCell){ rootRow, rootColumn };
                    collisionCenter = RpgStage_GetWorldPositionForCell(stage, rootRow, rootColumn);
                }
                MoveZipperCollisionCenterTo(&candidate, collisionCenter);
            }
            *velocity = (Vector2){ 0.0f, 0.0f };
            *isLaunched = false;
            *isAttachedToBlock = true;
            *attachedAttachmentIndex = -1;
            *zipper = candidate;
            return;
        }
        *zipper = candidate;
    }
}

static void UpdateZipperAttachedToDataShot(RpgZipper *zipper, const RpgDataShots *shots,
                                           int *attachedDataShotIndex,
                                           Vector2 attachedDataShotOffset,
                                           bool *zipperFollowsPlayer, bool *isAttachedToBlock,
                                           RpgGridCell *attachedBlockCell)
{
    if (*attachedDataShotIndex < 0) return;
    const RpgDataShot *shot = &shots->entries[*attachedDataShotIndex];
    // 電気化した時点で弾本体は壁衝突として消費済みなので、Zipper は追従へ戻す。
    if (shot->isElectric) {
        *zipperFollowsPlayer = true;
        *isAttachedToBlock = false;
        *attachedDataShotIndex = -1;
        return;
    }
    if (shot->active) {
        zipper->character.position = Vector2Add(shot->position, attachedDataShotOffset);
        return;
    }
    // 弾が壁へ当たって消えた場合は、その衝突位置へ残す。空中消滅なら主人公へ戻す。
    if (shot->hitWall) {
        MoveZipperCollisionCenterTo(zipper, shot->impactPosition);
        attachedBlockCell->row = (int)floorf(shot->impactPosition.y / RPG_STAGE_TILE_SIZE);
        attachedBlockCell->column = (int)floorf(shot->impactPosition.x / RPG_STAGE_TILE_SIZE);
        *isAttachedToBlock = true;
    } else {
        *zipperFollowsPlayer = true;
        *isAttachedToBlock = false;
    }
    *attachedDataShotIndex = -1;
}

Rectangle RpgRuntime_GetStopButtonBounds(void)
{
    const float width = 92.0f;
    const float height = 26.0f;
    return (Rectangle){ (float)RpgViewport_GetWidth() - width - 12.0f,
                        (float)RpgViewport_GetHeight() - height - 12.0f,
                        width, height };
}

static void DrawRpgWorld(const RpgCharacter *player, const RpgCharacter *npc,
                         const RpgStage *stage,
                         const RpgMagnetRuntime *magnetRuntime, int currentMapIndex, Camera2D camera, bool canTalk, const RpgDialogue *dialogue,
                         int dialogueIndex, int stage3IntroIndex, const RpgStage3Event *stage3Event, const RpgZipper *zipper,
                         Texture2D zipperTexture, Texture2D fileTexture, const RpgInspect *inspect, const RpgInspect *zipperInspect,
                         int inspectTarget, int inspectFunctionIndex, int inspectLineIndex,
                          bool isMoveSpriteVisible, float moveSpriteX, RpgInspectMoveTarget moveSpriteTarget,
                          bool npcInspectCompleted, bool zipperInspectCompleted,
                          bool isZipperPointerHovered, bool isZipperPointerSelected,
                          const RpgItems *items,
                          const RpgReferenceObjects *referenceDrops,
                          const RpgWires *wires,
                          const RpgReceivers *receivers,
                          const RpgAttachments *attachments,
                          const RpgDataShots *dataShots,
                          const RpgMapEvents *events, const char *itemMessage, float itemMessageTimer,
                          RpgReferenceTarget nearbyReferenceTarget,
                          const char *referenceFileName, const char *referenceText, bool isReferenceTextOpen,
                          RpgReferenceTarget hoveredReferenceTarget,
                          RpgReferenceTarget selectedReferenceTarget,
                          bool isReferencePointerFeedbackSuppressed,
                          bool isReferenceDragActive, RpgReferenceTarget draggedReferenceTarget,
                          Vector2 referenceDragPosition,
                          float zipperAnimationElapsed, const RpgStageBackground *stageBackground,
                          float backgroundBrightness, float blockBrightness,
                          bool isZipperLaunched, RpgCharacterAnimation playerAnimation,
                           bool showStopButton, RpgSceneState *scene, unsigned int zipperMaxCapacityKB,
                           bool isZipperConnected)
{
    (void)npcInspectCompleted;
    (void)zipperInspectCompleted;
    (void)stage3IntroIndex;
    RpgViewport_BeginFrame();
    ClearBackground(BLACK);
    BeginMode2D(camera);
    DrawConnectedStageMaps(stage, magnetRuntime, wires, receivers, attachments, dataShots,
                           fileTexture, stageBackground, backgroundBrightness, blockBrightness);
#if 0
    // 背景を各エリアのマス座標で描画し、共有ランタイムでもブロックと一致させる。
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        if (!RpgStage_IsMapActive(stage, mapIndex)) continue;
        RpgStageBackground_Draw(stageBackground,
                                (Rectangle){ (float)(mapIndex * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                             0.0f,
                                             (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE),
                                             (float)(RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE) },
                                backgroundBrightness);
    }
    /* 最背面PNGは背景の後、ブロックより前に描画する。 */
    RpgImageObjects_DrawLayer(&stage->imageObjects, 0, RPG_STAGE_WORLD_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_BACK);
    RpgStage_Draw(stage, false, blockBrightness);
    RpgMagnets_DrawMetals(magnetRuntime, 0, RPG_STAGE_WORLD_COLUMNS, 0.0f,
                          blockBrightness);
    // 通常ブロックへ紐づけたファイルは、そのブロック自体を強調する。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        RpgObjectFolder objectFolder = { .cell = { row, column } };
        if (!RpgObjectFolder_BlockHasLinkedFiles(&objectFolder, stage->blocks[row][column])) continue;
        Rectangle objectBounds = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                   RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        DrawRectangleRec(objectBounds, Fade(GOLD, 0.30f));
        DrawRectangleLinesEx(objectBounds, 2.0f, ORANGE);
    }
    // ファイルを紐づけた設置物だけを、実際の描画位置に合わせて強調する。
    for (int index = 0; index < attachments->count; index++) {
        const RpgAttachment *attachment = &attachments->entries[index];
        if (attachment->isZipperHeld) continue;
        if (!RpgObjectFolder_AttachmentHasLinkedFiles(attachment)) continue;
        Vector2 position = RpgStage_SnapRenderPoint(RpgAttachments_GetPosition(attachment, 0));
        DrawCircleV(position, 15.0f, Fade(GOLD, 0.30f));
        DrawCircleLines((int)position.x, (int)position.y, 15.0f, ORANGE);
    }
    for (int index = 0; index < RPG_DATA_SHOT_MAX_COUNT; index++) {
        const RpgDataShot *shot = &dataShots->entries[index];
        if (!shot->active || !RpgObjectFolder_DataShotHasLinkedFiles(shot)) continue;
        DrawCircleV(shot->position, shot->size + 8.0f, Fade(GOLD, 0.30f));
        DrawCircleLines((int)shot->position.x, (int)shot->position.y, shot->size + 8.0f, ORANGE);
    }
    RpgStage_DrawReferenceObjectsExcept(stage, fileTexture,
                                        isReferenceDragActive && draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_CELL ?
                                            draggedReferenceTarget.row : -1,
                                        isReferenceDragActive && draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_CELL ?
                                            draggedReferenceTarget.column : -1);
 #endif
    int excludedReferenceDropIndex = isReferenceDragActive &&
        draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_DROP ? draggedReferenceTarget.dropIndex :
        GetReferenceFolderTransferExcludedIndex(referenceDrops);
    RpgReferenceObjects_DrawExcept(referenceDrops, fileTexture, excludedReferenceDropIndex);
    if (GetReferenceFolderTransferExcludedIndex(referenceDrops) >= 0) {
        Vector2 transferPosition = GetReferenceFolderTransferPosition();
        float transferSize = 48.0f * referenceFolderTransfer.drawScale;
        RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ transferPosition.x - transferSize * 0.5f,
                                      transferPosition.y - transferSize * 0.5f, transferSize, transferSize }, WHITE);
    }
    if (isReferenceDragActive && draggedReferenceTarget.kind == RPG_REFERENCE_TARGET_CELL)
        RpgStage_DrawReferenceObject(fileTexture, GetReferenceTargetBounds(stage, referenceDrops, draggedReferenceTarget),
                                     Fade(WHITE, 0.28f));
    RpgItems_Draw(items);
    RpgMapEvents_Draw(events);
#if 0
    RpgStage_DrawEffects(stage);
    RpgMagnets_DrawFields(stage, 0, RPG_STAGE_WORLD_COLUMNS);
    RpgWires_Draw(wires, stage);
    RpgWires_DrawElectric(wires, dataShots, 0, RPG_STAGE_WORLD_COLUMNS);
    RpgReceivers_Draw(receivers);
    RpgAttachments_Draw(attachments);
    RpgDataShots_Draw(dataShots);
    /* 中間PNGはブロックより前、キャラクターより後に固定する。 */
    RpgImageObjects_DrawLayer(&stage->imageObjects, 0, RPG_STAGE_WORLD_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE,
                              RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK);
#endif
    DrawZipper(zipperTexture, &zipper->character, zipperAnimationElapsed);
    RpgRuntime_DrawZipperFolderReturn(zipper);
    if (isMoveSpriteVisible) DrawMoveSprite(zipperTexture, player, npc, zipper, moveSpriteTarget, moveSpriteX);
    RpgCharacter_Draw(npc, "NPC");
    RpgCharacter_DrawPlayer(player, isZipperLaunched ? RPG_CHARACTER_ANIMATION_ZIPGO : playerAnimation);
    /* 最前面PNGはキャラクター描画後に重ねる。 */
#if 0
    RpgImageObjects_DrawLayer(&stage->imageObjects, 0, RPG_STAGE_WORLD_COLUMNS,
                              RPG_STAGE_TILE_SIZE, WHITE, RPG_IMAGE_OBJECT_LAYER_FRONT);
#endif
    RpgZipper_DrawPointerFeedback(RpgZipper_GetPixelAlignedSpriteBounds(&zipper->character, 380.0f),
                                  isZipperPointerHovered, isZipperPointerSelected);
    if (!isReferencePointerFeedbackSuppressed && selectedReferenceTarget.kind != RPG_REFERENCE_TARGET_NONE) {
        Rectangle selectedBounds = GetReferenceTargetBounds(stage, referenceDrops, selectedReferenceTarget);
        RpgZipper_DrawPointerFeedback(selectedBounds, false, true);
    }
    if (!isReferencePointerFeedbackSuppressed && hoveredReferenceTarget.kind != RPG_REFERENCE_TARGET_NONE) {
        Rectangle hoveredBounds = GetReferenceTargetBounds(stage, referenceDrops, hoveredReferenceTarget);
        bool isSelected = hoveredReferenceTarget.kind == selectedReferenceTarget.kind &&
            hoveredReferenceTarget.row == selectedReferenceTarget.row &&
            hoveredReferenceTarget.column == selectedReferenceTarget.column &&
            hoveredReferenceTarget.dropIndex == selectedReferenceTarget.dropIndex;
        RpgZipper_DrawPointerFeedback(hoveredBounds, true, isSelected);
    }
    if (isReferenceDragActive) {
        Rectangle draggedBounds = GetReferenceTargetBounds(stage, referenceDrops, draggedReferenceTarget);
        float draggedSize = draggedBounds.width > 0.0f ? draggedBounds.width : RPG_STAGE_TILE_SIZE;
        RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ referenceDragPosition.x - draggedSize * 0.5f,
                                                                referenceDragPosition.y - draggedSize * 0.5f,
                                                                draggedSize, draggedSize }, WHITE);
    }
    if (canTalk && dialogueIndex < 0) {
        DrawRectangle((int)npc->position.x - 48, (int)npc->position.y - 116, 96, 24,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw("[E] 話しかける", npc->position.x - 48, npc->position.y - 112, 16, MAROON);
    }
    if (canTalk && dialogueIndex < 0) {
        DrawRectangle((int)npc->position.x - 58, (int)npc->position.y - 116, 116, 24,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw(npcTalkPrompt, npc->position.x - 54, npc->position.y - 112, 16, MAROON);
    }
    if (nearbyReferenceTarget.kind != RPG_REFERENCE_TARGET_NONE && !isReferenceTextOpen) {
        Rectangle referenceBounds = GetReferenceTargetBounds(stage, referenceDrops, nearbyReferenceTarget);
        float x = referenceBounds.x + referenceBounds.width * 0.5f;
        float y = referenceBounds.y - 38.0f;
        float fileNameWidth = GameFont_MeasureText(referenceFileName, 16.0f).x;
        DrawRectangle((int)(x - fileNameWidth / 2.0f - 6.0f), (int)y, (int)fileNameWidth + 12, 44,
                      Fade(RAYWHITE, 0.9f));
        GameFont_Draw(referenceFileName, x - fileNameWidth / 2.0f, y + 3.0f, 16.0f, DARKBLUE);
        if (IsReferenceFolderTarget(stage, nearbyReferenceTarget)) {
            const char *folderPrompt = RpgReferenceObjects_FindFollowerIndex(referenceDrops) >= 0 ?
                                       "[E] Open  [P] Store" : "[E] Open";
            GameFont_Draw(folderPrompt, x - 48.0f, y + 22.0f, 15.0f, MAROON);
        } else {
            GameFont_Draw("[E] Open  [G] Acquire", x - 48.0f, y + 22.0f, 15.0f, MAROON);
        }
    }
    RpgNearbyKeyDoor nearbyKeyDoor = FindNearbyKeyDoor(stage, player->position, 72.0f);
    if (nearbyKeyDoor.row >= 0 && !isReferenceTextOpen) {
        Rectangle doorBounds = RpgStage_GetWorldBoundsForCell(stage, nearbyKeyDoor.row, nearbyKeyDoor.column);
        float promptX = doorBounds.x - 18.0f;
        float promptY = doorBounds.y - 24.0f;
        DrawRectangle((int)promptX, (int)promptY, 94, 22, Fade(RAYWHITE, 0.90f));
        GameFont_Draw("[E] Examine", promptX + 5.0f, promptY + 3.0f, 14.0f, MAROON);
    }
    EndMode2D();
    // 本編のワールドと常設操作UIを混在させない。上部帯は不透明にして、ゲーム描画をUIの背後へ見せない。
    // 本編は20x12マスを画面全体へ使う。常設のエリア表示・操作説明・カメラ切替帯は描画しない。
    /* 実 Zipper フォルダの変更通知で更新される、ゲーム共通のストレージ表示。 */
    if (isZipperConnected) {
        unsigned long long usedBytes = RpgObjectFolder_GetZipperStorageBytes();
        unsigned long long maxBytes = (unsigned long long)(zipperMaxCapacityKB == 0 ? 1U : zipperMaxCapacityKB) * 1000ULL;
        float ratio = Clamp((float)((double)usedBytes / (double)maxBytes), 0.0f, 1.0f);
        Rectangle panel = { 12.0f, 12.0f, 174.0f, 48.0f };
        Rectangle track = { 22.0f, 40.0f, 154.0f, 7.0f };
        Color storageColor = usedBytes > maxBytes ? MAROON : DARKGREEN;
        DrawRectangleRounded(panel, 0.16f, 8, Fade(BLACK, 0.78f));
        DrawRectangleLinesEx(panel, 1.0f, Fade(RAYWHITE, 0.42f));
        DrawText("ZIPPER", 22, 19, 13, RAYWHITE);
        DrawText(TextFormat("%.2f / %u KB", (double)usedBytes / 1000.0, zipperMaxCapacityKB),
                 82, 20, 11, usedBytes > maxBytes ? ORANGE : LIGHTGRAY);
        DrawRectangleRounded(track, 0.8f, 6, Fade(LIGHTGRAY, 0.36f));
        if (ratio > 0.0f)
            DrawRectangleRounded((Rectangle){ track.x, track.y, track.width * ratio, track.height },
                                 0.8f, 6, storageColor);
    }
    if (itemMessageTimer > 0.0f) GameFont_Draw(itemMessage, 300, 90, 24, MAROON);
    if (isReferenceTextOpen) DrawReferenceTextPanel(referenceFileName, referenceText);
    const RpgInspect *entryInspect = stage3Event != NULL ? &stage3Event->inspect : inspect;
    const RpgInspect *activeInspect = inspectTarget == 2 ? zipperInspect :
                                     inspectTarget == 3 ? entryInspect : inspect;
    bool isInspectDialogue = inspectTarget >= 0 &&
                             activeInspect->functions[inspectFunctionIndex].type == RPG_INSPECT_DIALOGUE;
    if (dialogueIndex >= 0 || isInspectDialogue) {
        const Rectangle dialogBounds = { 150.0f, 350.0f, 660.0f, 130.0f };
        const Rectangle speakerBounds = { 174.0f, 332.0f, 150.0f, 36.0f };
        DrawRectangleRec(dialogBounds, Fade(RAYWHITE, 0.96f));
        DrawRectangleLinesEx(dialogBounds, 2.0f, DARKBLUE);
        DrawRectangleRec(speakerBounds, DARKBLUE);
        const RpgDialogue *inspectDialogue = isInspectDialogue ? &activeInspect->functions[inspectFunctionIndex].dialogue : NULL;
        const char *speaker = inspectDialogue != NULL ? inspectDialogue->speakers[inspectLineIndex] : dialogue->speakers[dialogueIndex];
        const char *text = inspectDialogue != NULL ? inspectDialogue->lines[inspectLineIndex] : dialogue->lines[dialogueIndex];
        GameFont_Draw(speaker, 190, 340, 21, RAYWHITE);
        GameFont_Draw(text, 178, 390, 24, DARKBLUE);
        GameFont_Draw(inspectDialogue != NULL ? TextFormat("E: next  function %d / %d, line %d / %d", inspectFunctionIndex + 1, activeInspect->functionCount, inspectLineIndex + 1, inspectDialogue->lineCount) : TextFormat("E: 次へ  %d / %d", dialogueIndex + 1, dialogue->lineCount),
                      178, 432, 17, GRAY);
    }
    if (showStopButton) {
        Rectangle playerAreaBounds = { 12.0f, 12.0f, 182.0f, 28.0f };
        DrawRectangleRec(playerAreaBounds, Fade(BLACK, 0.72f));
        DrawRectangleLinesEx(playerAreaBounds, 1.0f, SKYBLUE);
        DrawText(TextFormat("Player Area[%d][%d]", stage->mapGridX[currentMapIndex],
                            stage->mapGridY[currentMapIndex]),
                 (int)playerAreaBounds.x + 7, (int)playerAreaBounds.y + 7, 14, RAYWHITE);

        Rectangle stopButtonBounds = RpgRuntime_GetStopButtonBounds();
        DrawRectangleRec(stopButtonBounds, MAROON);
        DrawRectangleLinesEx(stopButtonBounds, 1.0f, RAYWHITE);
        DrawText("Stop [F2]", (int)stopButtonBounds.x + 7, (int)stopButtonBounds.y + 6, 15, RAYWHITE);
    }
    if (RpgScene_IsGameSettings(scene)) RpgScene_DrawGameSettingsOverlay(scene);
    else if (scene != NULL) RpgScene_DrawGameSettingsButton();
    RpgViewport_EndFrame();
}

/* NPC、Zipper、入場イベントで同じ Function 実行器を使うための対象選択。 */
static RpgInspect *GetRuntimeActiveInspect(RpgRuntimeContext *context)
{
    if (*context->inspectTarget == 2) return &context->zipper->inspect;
    if (*context->inspectTarget == 3 && context->activeEntryEvent != NULL &&
        *context->activeEntryEvent != NULL) return &(*context->activeEntryEvent)->inspect;
    return context->inspect;
}

enum { RPG_RUNTIME_ACTIVE_MOVE_COUNT = RPG_INSPECT_MAX_FUNCTIONS };

typedef struct RpgRuntimeMoveState {
    RpgInspectMove *move;
    float elapsed;
    float startX;
    float startY;
    float transitionElapsed;
    bool running;
    bool transitioned;
} RpgRuntimeMoveState;

/* Function列は複数のMoveを同時に開始できる。実行状態は保存データではなくランタイム専用。 */
static RpgRuntimeMoveState runtimeInspectMoves[RPG_RUNTIME_ACTIVE_MOVE_COUNT] = { 0 };
static const RpgStage *runtimeInspectMoveStage = NULL;

static void ResetRuntimeInspectMovesIfStageChanged(const RpgRuntimeContext *context)
{
    if (context == NULL) return;
    if (runtimeInspectMoveStage != context->stage ||
        (context->activeInspectMove != NULL && context->isInspectMoveRunning != NULL &&
         *context->activeInspectMove == NULL && !*context->isInspectMoveRunning)) {
        memset(runtimeInspectMoves, 0, sizeof(runtimeInspectMoves));
        runtimeInspectMoveStage = context->stage;
    }
}

static bool HasRunningRuntimeInspectMove(void)
{
    for (int index = 0; index < RPG_RUNTIME_ACTIVE_MOVE_COUNT; index++)
        if (runtimeInspectMoves[index].move != NULL && runtimeInspectMoves[index].running) return true;
    return false;
}

static bool HasRunningPlayerWalkMove(void)
{
    for (int index = 0; index < RPG_RUNTIME_ACTIVE_MOVE_COUNT; index++)
        if (runtimeInspectMoves[index].move != NULL && runtimeInspectMoves[index].running &&
            runtimeInspectMoves[index].move->target == RPG_INSPECT_MOVE_PLAYER &&
            runtimeInspectMoves[index].move->walkAnimationEnabled) return true;
    return false;
}

static void SyncLegacyRuntimeMoveState(RpgRuntimeContext *context)
{
    RpgRuntimeMoveState *representative = NULL;
    for (int index = 0; index < RPG_RUNTIME_ACTIVE_MOVE_COUNT; index++)
        if (runtimeInspectMoves[index].move != NULL) {
            representative = &runtimeInspectMoves[index];
            if (representative->running) break;
        }
    if (context->isInspectMoveRunning != NULL) *context->isInspectMoveRunning = HasRunningRuntimeInspectMove();
    if (context->activeInspectMove != NULL) *context->activeInspectMove = representative != NULL ? representative->move : NULL;
    if (representative != NULL) {
        if (context->inspectMoveElapsed != NULL) *context->inspectMoveElapsed = representative->elapsed;
        if (context->inspectMoveStartX != NULL) *context->inspectMoveStartX = representative->startX;
        if (context->inspectMoveStartY != NULL) *context->inspectMoveStartY = representative->startY;
        if (context->inspectMoveTransitionElapsed != NULL)
            *context->inspectMoveTransitionElapsed = representative->transitionElapsed;
    }
}

static void CompleteRuntimeInspect(RpgRuntimeContext *context)
{
    /* Function列の完了は対象種別に依存しない。入力を止める全実行状態をここだけで解放する。 */
    bool keepsRunningMove = context->isInspectMoveRunning != NULL && *context->isInspectMoveRunning;
    /* 先行MoveはFunction列の終了後も最後まで更新する。終了済みのMoveだけをここで破棄する。 */
    if (!keepsRunningMove && context->activeInspectMove != NULL) *context->activeInspectMove = NULL;
    if (context->inspectMoveTransitionElapsed != NULL) *context->inspectMoveTransitionElapsed = 0.0f;
    if (context->activeWaitFunctionIndex != NULL) *context->activeWaitFunctionIndex = -1;
    if (context->inspectWaitElapsed != NULL) *context->inspectWaitElapsed = 0.0f;
    if (context->stage3IntroIndex != NULL) *context->stage3IntroIndex = -1;
    if (!keepsRunningMove && context->player != NULL) context->player->isMoving = false;
    if (*context->inspectTarget == 2) {
        *context->zipperFollowsPlayer = true;
        *context->isZipperControllable = true;
        *context->zipperInspectCompleted = true;
    } else if (*context->inspectTarget == 1) {
        *context->npcInspectCompleted = true;
    }
    *context->inspectTarget = -1;
    *context->inspectFunctionIndex = -1;
    *context->inspectLineIndex = -1;
}

/* Moveの進行状態をFunction列から独立して保持し、次のFunctionへ進んだ後も移動を続ける。 */
/* 旧単一Move実装。複数Move状態へ移行済みのため、互換参照としてのみ残す。 */
#if 0
static bool StartRuntimeInspectMoveLegacy(RpgRuntimeContext *context, RpgInspectMove *move)
{
    if (context == NULL || move == NULL || context->activeInspectMove == NULL ||
        context->inspectMoveTransitionElapsed == NULL) return false;
    /* activeが同じでrunning=falseなら、移動済みで次Function待機中。二重開始しない。 */
    if (*context->activeInspectMove == move) return *context->isInspectMoveRunning;
    if (*context->isInspectMoveRunning) return false;
    int imageIndex = move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT ?
                     RpgImageObjects_FindById(&context->stage->imageObjects, move->targetImageObjectId) : -1;
    RpgImageObject *imageTarget = imageIndex >= 0 ? &context->stage->imageObjects.entries[imageIndex] : NULL;
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && imageTarget == NULL)
        move->target = RPG_INSPECT_MOVE_PLAYER;
    *context->activeInspectMove = move;
    *context->isInspectMoveRunning = true;
    *context->inspectMoveElapsed = 0.0f;
    *context->inspectMoveTransitionElapsed = 0.0f;
    *context->inspectMoveStartX = imageTarget != NULL ?
                                RpgImageObjects_GetWorldCenterX(imageTarget, RPG_STAGE_TILE_SIZE) :
                                move->target == RPG_INSPECT_MOVE_PLAYER ? context->player->position.x :
                                move->target == RPG_INSPECT_MOVE_NPC ? context->npc->position.x : context->zipper->character.position.x;
    *context->inspectMoveStartY = imageTarget != NULL ?
                                RpgImageObjects_GetWorldCenterY(imageTarget, RPG_STAGE_TILE_SIZE) :
                                move->target == RPG_INSPECT_MOVE_PLAYER ? context->player->position.y :
                                move->target == RPG_INSPECT_MOVE_NPC ? context->npc->position.y : context->zipper->character.position.y;
    return true;
}

/* 進行中Moveだけを更新する。Functionの選択位置に依存しないため、Dialogueへ進んでも止まらない。 */
static void UpdateRuntimeInspectMoveLegacy(RpgRuntimeContext *context, float deltaTime)
{
    if (context == NULL || context->activeInspectMove == NULL || !*context->isInspectMoveRunning ||
        *context->activeInspectMove == NULL) return;
    RpgInspectMove *move = *context->activeInspectMove;
    int imageIndex = move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT ?
                     RpgImageObjects_FindById(&context->stage->imageObjects, move->targetImageObjectId) : -1;
    RpgImageObject *imageTarget = imageIndex >= 0 ? &context->stage->imageObjects.entries[imageIndex] : NULL;
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && imageTarget == NULL)
        move->target = RPG_INSPECT_MOVE_PLAYER;
    *context->inspectMoveElapsed += deltaTime;
    float progress = Clamp(*context->inspectMoveElapsed / move->duration, 0.0f, 1.0f);
    float easedProgress = RpgInspect_EaseMoveProgress(move->easing, progress);
    float currentX = RpgInspect_MoveAxisHasX(move->axis) ?
        *context->inspectMoveStartX + (move->destinationX - *context->inspectMoveStartX) * easedProgress :
        *context->inspectMoveStartX;
    float currentY = RpgInspect_MoveAxisHasY(move->axis) ?
        *context->inspectMoveStartY + (move->destinationY - *context->inspectMoveStartY) * easedProgress :
        *context->inspectMoveStartY;
    if (imageTarget != NULL) RpgImageObjects_SetRuntimePosition(imageTarget, (Vector2){ currentX, currentY });
    else {
        float *targetX = move->target == RPG_INSPECT_MOVE_PLAYER ? &context->player->position.x :
                         move->target == RPG_INSPECT_MOVE_NPC ? &context->npc->position.x : &context->zipper->character.position.x;
        float *targetY = move->target == RPG_INSPECT_MOVE_PLAYER ? &context->player->position.y :
                         move->target == RPG_INSPECT_MOVE_NPC ? &context->npc->position.y : &context->zipper->character.position.y;
        *targetX = currentX;
        *targetY = currentY;
    }
    if (move->target == RPG_INSPECT_MOVE_PLAYER) {
        context->player->isMoving = move->walkAnimationEnabled;
        if (move->walkAnimationEnabled) {
            context->player->animationElapsed += deltaTime * move->walkAnimationSpeed;
            if (fabsf(currentX - *context->inspectMoveStartX) > 0.01f)
                context->player->facingDirection = currentX >= *context->inspectMoveStartX ? 1 : -1;
        }
    }
    if (progress >= 1.0f) {
        if (imageTarget != NULL)
            RpgImageObjects_CommitRuntimePosition(imageTarget, RPG_STAGE_TILE_SIZE, RPG_STAGE_WORLD_COLUMNS,
                                                  RPG_STAGE_ROWS);
        /* activeは遷移待ちの識別として残す。次Functionへ進んだ時点で解放する。 */
        *context->isInspectMoveRunning = false;
        if (move->target == RPG_INSPECT_MOVE_PLAYER) context->player->isMoving = false;
    }
}

/* 完了済みの即時Functionを同一フレームで連結する。時間を必要とするFunctionだけが次フレームへ継続する。 */
#endif

static RpgRuntimeMoveState *StartRuntimeInspectMove(RpgRuntimeContext *context, RpgInspectMove *move)
{
    if (context == NULL || move == NULL) return NULL;
    ResetRuntimeInspectMovesIfStageChanged(context);
    for (int index = 0; index < RPG_RUNTIME_ACTIVE_MOVE_COUNT; index++)
        if (runtimeInspectMoves[index].move == move && !runtimeInspectMoves[index].transitioned)
            return &runtimeInspectMoves[index];
    RpgRuntimeMoveState *state = NULL;
    for (int index = 0; index < RPG_RUNTIME_ACTIVE_MOVE_COUNT; index++)
        if (runtimeInspectMoves[index].move == NULL) { state = &runtimeInspectMoves[index]; break; }
    if (state == NULL) return NULL;
    int imageIndex = move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT ?
                     RpgImageObjects_FindById(&context->stage->imageObjects, move->targetImageObjectId) : -1;
    RpgImageObject *imageTarget = imageIndex >= 0 ? &context->stage->imageObjects.entries[imageIndex] : NULL;
    if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && imageTarget == NULL) move->target = RPG_INSPECT_MOVE_PLAYER;
    *state = (RpgRuntimeMoveState){
        .move = move, .running = true,
        .startX = imageTarget != NULL ? RpgImageObjects_GetWorldCenterX(imageTarget, RPG_STAGE_TILE_SIZE) :
                  move->target == RPG_INSPECT_MOVE_PLAYER ? context->player->position.x :
                  move->target == RPG_INSPECT_MOVE_NPC ? context->npc->position.x : context->zipper->character.position.x,
        .startY = imageTarget != NULL ? RpgImageObjects_GetWorldCenterY(imageTarget, RPG_STAGE_TILE_SIZE) :
                  move->target == RPG_INSPECT_MOVE_PLAYER ? context->player->position.y :
                  move->target == RPG_INSPECT_MOVE_NPC ? context->npc->position.y : context->zipper->character.position.y
    };
    SyncLegacyRuntimeMoveState(context);
    return state;
}

static void UpdateRuntimeInspectMove(RpgRuntimeContext *context, float deltaTime)
{
    if (context == NULL) return;
    ResetRuntimeInspectMovesIfStageChanged(context);
    bool playerWalkActive = false;
    for (int index = 0; index < RPG_RUNTIME_ACTIVE_MOVE_COUNT; index++) {
        RpgRuntimeMoveState *state = &runtimeInspectMoves[index];
        if (state->move == NULL || !state->running) continue;
        RpgInspectMove *move = state->move;
        int imageIndex = move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT ?
                         RpgImageObjects_FindById(&context->stage->imageObjects, move->targetImageObjectId) : -1;
        RpgImageObject *imageTarget = imageIndex >= 0 ? &context->stage->imageObjects.entries[imageIndex] : NULL;
        if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && imageTarget == NULL) move->target = RPG_INSPECT_MOVE_PLAYER;
        state->elapsed += deltaTime;
        float progress = Clamp(state->elapsed / move->duration, 0.0f, 1.0f);
        float easedProgress = RpgInspect_EaseMoveProgress(move->easing, progress);
        float currentX = RpgInspect_MoveAxisHasX(move->axis) ? state->startX + (move->destinationX - state->startX) * easedProgress : state->startX;
        float currentY = RpgInspect_MoveAxisHasY(move->axis) ? state->startY + (move->destinationY - state->startY) * easedProgress : state->startY;
        if (imageTarget != NULL) RpgImageObjects_SetRuntimePosition(imageTarget, (Vector2){ currentX, currentY });
        else {
            float *targetX = move->target == RPG_INSPECT_MOVE_PLAYER ? &context->player->position.x : move->target == RPG_INSPECT_MOVE_NPC ? &context->npc->position.x : &context->zipper->character.position.x;
            float *targetY = move->target == RPG_INSPECT_MOVE_PLAYER ? &context->player->position.y : move->target == RPG_INSPECT_MOVE_NPC ? &context->npc->position.y : &context->zipper->character.position.y;
            *targetX = currentX;
            *targetY = currentY;
        }
        if (move->target == RPG_INSPECT_MOVE_PLAYER && move->walkAnimationEnabled) {
            playerWalkActive = true;
            context->player->animationElapsed += deltaTime * move->walkAnimationSpeed;
            if (fabsf(currentX - state->startX) > 0.01f) context->player->facingDirection = currentX >= state->startX ? 1 : -1;
        }
        if (progress >= 1.0f) {
            if (imageTarget != NULL) RpgImageObjects_CommitRuntimePosition(imageTarget, RPG_STAGE_TILE_SIZE, RPG_STAGE_WORLD_COLUMNS, RPG_STAGE_ROWS);
            state->running = false;
            if (state->transitioned) state->move = NULL;
        }
    }
    /* 通常入力の移動状態はRpgRuntime_UpdateWorldが所有する。イベントMoveが
       実行中の間だけここで上書きし、Moveが無い通常フレームで停止状態を壊さない。 */
    if (playerWalkActive || *context->inspectTarget >= 0)
        context->player->isMoving = playerWalkActive;
    SyncLegacyRuntimeMoveState(context);
}

static void AdvanceRuntimeInspectFunctions(RpgRuntimeContext *context, float deltaTime)
{
    if (context == NULL) return;
    for (int iteration = 0; iteration < RPG_INSPECT_MAX_FUNCTIONS && *context->inspectTarget >= 0; iteration++) {
        RpgInspect *inspect = GetRuntimeActiveInspect(context);
        if (*context->inspectFunctionIndex < 0 ||
            *context->inspectFunctionIndex >= inspect->functionCount) {
            CompleteRuntimeInspect(context);
            return;
        }
        RpgInspectFunction *function = &inspect->functions[*context->inspectFunctionIndex];
        if (function->type == RPG_INSPECT_DIALOGUE) return;
        if (function->type == RPG_INSPECT_MOVE) {
            RpgInspectMove *move = &function->move;
            RpgRuntimeMoveState *moveState = StartRuntimeInspectMove(context, move);
            /* Moveは完了を待たず、開始から設定秒数で次Functionへ渡す。実行中の移動は
               activeInspectMoveが引き続き更新するため、待ち時間0では即座に連結できる。 */
            if (moveState == NULL) return;
            moveState->transitionElapsed += deltaTime;
            if (moveState->transitionElapsed < move->nextFunctionDelay) return;
            moveState->transitioned = true;
            if (!moveState->running) moveState->move = NULL;
        } else if (function->type == RPG_INSPECT_WAIT) {
            if (function->wait.duration > 0.0f) {
                if (*context->activeWaitFunctionIndex != *context->inspectFunctionIndex) {
                    *context->activeWaitFunctionIndex = *context->inspectFunctionIndex;
                    *context->inspectWaitElapsed = 0.0f;
                }
                *context->inspectWaitElapsed += deltaTime;
                if (*context->inspectWaitElapsed < function->wait.duration) return;
            }
            *context->activeWaitFunctionIndex = -1;
            *context->inspectWaitElapsed = 0.0f;
        } else if (function->type == RPG_INSPECT_LAYER_CHANGE) {
            int imageIndex = RpgImageObjects_FindById(&context->stage->imageObjects,
                                                      function->layerChange.targetImageObjectId);
            if (imageIndex >= 0)
                context->stage->imageObjects.entries[imageIndex].layer =
                    Clamp(function->layerChange.layer, 0, 2);
        } else return;
        (*context->inspectFunctionIndex)++;
        (*context->inspectLineIndex) = 0;
        if (*context->inspectFunctionIndex >= inspect->functionCount) {
            CompleteRuntimeInspect(context);
            return;
        }
    }
}


void RpgRuntime_UpdateAndDraw(RpgRuntimeContext *context)
{
    if (context == NULL) return;
    InitializeRuntimeWorldCoordinates(context);
    // 本編の常設UIは設定ボタンだけを右上へ置き、マス表示用の下部帯を作らない。
    RpgScene_SetGameSettingsButtonBounds((Rectangle){ RpgViewport_GetWidth() - 106.0f, 12.0f,
                                                        94.0f, 26.0f });
    // 設定シーンでは本編の入力・物理・描画更新を実行せず、シーンUIだけを処理する。
    if (context->scene != NULL && RpgScene_IsGameSettings(context->scene)) {
        RpgScene_UpdateGameSettings(context->scene);
        // 戻る先が本編でもタイトルでも、このフレームで遷移先を必ず描画する。
        RpgScene_UpdateAndDraw(context->scene);
        return;
    }
    if (context->scene != NULL && RpgScene_TryOpenGameSettings(context->scene)) {
        RpgScene_UpdateAndDraw(context->scene);
        return;
    }
    (void)RegisterReferenceFileNames;
#define zipperPointerSelected (*context->zipperPointerSelected)
#define itemMessage (context->itemMessage)
#define zipperTexture (context->zipperTexture)
#define fileTexture (context->fileTexture)
    {
        bool hasPlayerMoveInput = IsKeyDown(KEY_A) || IsKeyDown(KEY_D) || IsKeyDown(KEY_LEFT) ||
                                  IsKeyDown(KEY_RIGHT) || IsKeyPressed(KEY_W);
        if (hasPlayerMoveInput && !(*context->isReferenceTextOpen)) {
            (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
            (*context->isReferencePointerFeedbackSuppressed) = true;
        }
        if ((*context->zipperFollowsPlayer) && hasPlayerMoveInput) {
            // 移動を始めた時は、Zipperの選択表示を通常状態へ戻す。
            zipperPointerSelected = false;
            (*context->isZipperPointerFeedbackSuppressed) = true;
        }
        /* 共通ランタイムへ移行済みの移動処理。信号・データ弾の更新は後段で従来どおり実行する。 */
#if 0
        if ((*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && (*context->inspectTarget) < 0 && !(*context->isReferenceTextOpen)) {
            Vector2 previousPosition = (*context->player).position;
            int standingBlockType = RpgStage_GetBlockTypeAtPosition(&(*context->stage), (*context->player).position);
            float savedMoveSpeed = (*context->player).moveSpeed;
            // 遅延ブロックの減速はプレイヤー設定値を変更せず、このフレームだけに適用する。
            if (standingBlockType == RPG_BLOCK_EFFECT_SLOW) (*context->player).moveSpeed *= 0.55f;
            RpgCharacter_UpdatePlayerWithStage(&(*context->player), GetFrameTime(), &(*context->stage), 32.0f,
                                               RPG_STAGE_WORLD_WIDTH - 32.0f);
            (*context->player).moveSpeed = savedMoveSpeed;
            // 跳ねるブロックは接地した瞬間に再び上向きの速度を与える。
            if (RpgBlockInventory_IsBounceEffect(standingBlockType) && (*context->player).isGrounded) {
                (*context->player).verticalSpeed = -620.0f;
                (*context->player).isGrounded = false;
            }
            if (CheckCollisionRecs(RpgCharacter_GetFootBounds(&(*context->player)),
                                   RpgCharacter_GetFootBounds(&(*context->npc)))) {
                (*context->player).position = previousPosition;
            }
        }
#endif
        /* ステージへビルドして入った直後のイベントは、最初の入力より前に一度だけ開始する。 */
        if (!(*context->stage3IntroShown) && (*context->stage3Event).inspect.enabled &&
            (*context->stage3Event).inspect.functionCount > 0) {
            if (context->activeEntryEvent != NULL) (*context->activeEntryEvent) = context->stage3Event;
            (*context->inspectTarget) = 3;
            (*context->inspectFunctionIndex) = 0;
            (*context->inspectLineIndex) = 0;
            (*context->stage3IntroShown) = true;
        }
        RpgRuntimeUpdateContext runtimeMovementContext = {
            .player = &(*context->player), .npc = &(*context->npc), .stage = &(*context->stage), .attachments = &(*context->attachments),
            .signalBlocks = &(*context->signalBlocks), .dataShots = &(*context->dataShots), .buttonEvent = &(*context->buttonEvent),
            .receivers = &(*context->receivers), .wires = &(*context->wires), .layout = &(*context->layout),
            .magnetRuntime = context->magnetRuntime,
            .playerPushState = &playerPushState,
            .wasButtonPressed = &(*context->wasDataButtonPressed),
            .currentMapIndex = GetRuntimeMapIndex(&(*context->stage), (*context->player).position,
                                                   *context->previousMap),
            .acceptsPlayerInput = (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 &&
                                  (*context->inspectTarget) < 0 && !(*context->isInspectMoveRunning) &&
                                  !(*context->isReferenceTextOpen),
            .updatesWorldSystems = false
        };
        // 信号・データ弾を含む更新は後段で1回だけ実行する。
        if (IsKeyPressed(KEY_SPACE) && (*context->isZipperControllable) && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && (*context->inspectTarget) < 0 &&
            !(*context->isReferenceTextOpen) && !(*context->isReferenceDragActive)) {
            if ((*context->zipperFollowsPlayer)) {
                // 射出開始位置は常に主人公。カーソルへ向かう単位ベクトルを固定して直進させる。
                (*context->zipper).character.position = (*context->player).position;
                Vector2 direction = Vector2Subtract(GetRuntimePointerWorldPosition((*context->camera), &(*context->stage),
                                                                                     GetRuntimeMapIndex(&(*context->stage), (*context->player).position,
                                                                                                        *context->previousMap)),
                                                    GetZipperCollisionCenter(&(*context->zipper)));
                if (Vector2LengthSqr(direction) < 0.001f) direction = (Vector2){ 1.0f, 0.0f };
                (*context->zipperLaunchVelocity) = Vector2Scale(Vector2Normalize(direction), (*context->zipper).launchSpeed);
                (*context->isZipperLaunched) = true;
                RpgCharacter_ResetAnimation(&(*context->player));
                (*context->zipperFollowsPlayer) = false;
                (*context->attachedDataShotIndex) = -1;
                (*context->attachedAttachmentIndex) = -1;
                (*context->isZipperAttachedToBlock) = false;
                (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
                zipperPointerSelected = false;
            } else if (!(*context->isZipperLaunched)) {
                /* 帰還は接触状態だけを解除する。Inboxの所持フォルダは次のcmdまで保持する。 */
                (*context->zipperFollowsPlayer) = true;
                (*context->attachedDataShotIndex) = -1;
                (*context->attachedAttachmentIndex) = -1;
                (*context->isZipperAttachedToBlock) = false;
                (*context->zipperAttachedBlockCell) = (RpgGridCell){ -1, -1 };
                zipperPointerSelected = false;
            }
        }
        /* 共通ランタイム側が信号・データ弾まで更新するため、旧更新列は残さない。 */
#if 0
        bool isDataButtonPressed = (*context->player).isGrounded &&
                                   RpgAttachments_IsButtonPressed(&(*context->attachments), (*context->player).position);
        if (isDataButtonPressed && !(*context->wasDataButtonPressed)) {
            // ボタンは用途を決めず、押された事実だけを全体通知として発行する。
            RpgButtonEvent_Publish(&(*context->buttonEvent));
        }
        (*context->wasDataButtonPressed) = isDataButtonPressed;
        RpgDataShots_ConsumeButtonEvent(&(*context->dataShots), &(*context->attachments), &(*context->buttonEvent));
        RpgSignalBlocks_Update(&(*context->signalBlocks), &(*context->stage), &(*context->buttonEvent), GetFrameTime());
        // フォルダの変更を移動前に反映し、ファイル数と容量が弾の見た目・速度へ直ちに反映されるようにする。
        RpgObjectFolders_UpdateDataShotLifetimes(&(*context->dataShots), &(*context->attachments), &(*context->referenceDrops));
        RpgDataShots_Update(&(*context->dataShots), &(*context->attachments), &(*context->stage), &(*context->receivers), &(*context->wires),
                             (*context->layout).electricCellDelay, NULL, GetFrameTime(), false);
#endif
        runtimeMovementContext.updatesWorldSystems = true;
        RpgRuntime_UpdateWorld(&runtimeMovementContext, GetFrameTime());
        /* Keep the area occupied before this movement step as the source of an
           edge transition.  A player crossing a storage-slot boundary must not
           be reclassified as the numerically next slot before the two-dimensional
           grid resolves its actual neighbour (an inserted area can live in any
           free storage slot). */
        int currentMapIndex = RpgStage_FindNearestActiveMap(&(*context->stage),
                                                            *context->previousMap);
        if (runtimeMovementContext.acceptsPlayerInput) {
            RpgAreaDirection direction;
            if (GetAreaMoveDirectionFromArrowKey(&direction))
                (void)MovePlayerToAdjacentArea(context, &currentMapIndex, direction);
        }
        TransitionPlayerBetweenAreas(context, &currentMapIndex);
        RpgObjectFolders_UpdateDataShotLifetimes(&(*context->dataShots), &(*context->attachments), &(*context->referenceDrops));
        // 電気化で失われた弾本体のフォルダを同フレームで処理し、追加ファイルをドロップする。
        RpgObjectFolders_UpdateDataShotLifetimes(&(*context->dataShots), &(*context->attachments), &(*context->referenceDrops));
        UpdateZipperAttachedToDataShot(&(*context->zipper), &(*context->dataShots), &(*context->attachedDataShotIndex),
                                       (*context->attachedDataShotOffset),
                                       &(*context->zipperFollowsPlayer), &(*context->isZipperAttachedToBlock),
                                       &(*context->zipperAttachedBlockCell));
        if ((*context->isZipperLaunched))
            UpdateLaunchedZipper(&(*context->zipper), &(*context->zipperLaunchVelocity), &(*context->stage), &(*context->attachments), &(*context->dataShots),
                                 GetFrameTime(), &(*context->isZipperLaunched), &(*context->attachedDataShotIndex),
                                 &(*context->attachedDataShotOffset),
                                 &(*context->isZipperAttachedToBlock), &(*context->zipperAttachedBlockCell),
                                 &(*context->attachedAttachmentIndex));
        else if ((*context->zipperFollowsPlayer) && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen))
            UpdateZipperFollow(&(*context->zipper), &(*context->player), GetFrameTime());
        RpgRuntime_ProcessReferenceFolderZipperCommand(context);
        RpgRuntime_ProcessZipperCommand(context);
        if ((*context->zipperAnimationElapsed) >= 0.0f) {
            (*context->zipperAnimationElapsed) += GetFrameTime();
            if ((*context->zipperAnimationElapsed) >= zipperImportAnimationDuration) (*context->zipperAnimationElapsed) = -1.0f;
        }
        RpgRuntime_UpdateZipperFolderReturn(context, GetFrameTime());
        bool canTalk = RpgCharacter_IsNear(&(*context->player), &(*context->npc), 72.0f);
        RpgReferenceTarget nearbyReferenceTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                       .row = -1, .column = -1, .dropIndex = -1 };
        RpgReferenceTarget nearbyFolderTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                    .row = -1, .column = -1, .dropIndex = -1 };
        RpgNearbyKeyDoor nearbyKeyDoor = FindNearbyKeyDoor(&(*context->stage), (*context->player).position, 72.0f);
        bool canReadReference = RpgReferenceObjects_FindNearbyTarget(&(*context->stage), &(*context->referenceDrops),
                                                                       (*context->player).position, 72.0f,
                                                                       &nearbyReferenceTarget);
        bool canStoreReference = RpgReferenceObjects_FindNearbyFolderTarget(&(*context->stage),
                                                                              (*context->player).position, 72.0f,
                                                                              &nearbyFolderTarget) &&
                                 RpgReferenceObjects_FindFollowerIndex(&(*context->referenceDrops)) >= 0;
        if (canReadReference) {
            const char *referencePath = RpgReferenceObjects_GetTargetPath(&(*context->stage), &(*context->referenceDrops),
                                                                           nearbyReferenceTarget);
            snprintf(context->referenceFileName, (size_t)context->referenceFileNameSize, "%s", GetReferenceFileName(referencePath));
            GameFont_AddText(context->referenceFileName);
        }
        if ((*context->itemMessageTimer) > 0.0f) (*context->itemMessageTimer) -= GetFrameTime();
        RpgReferenceObjects_Update(&(*context->referenceDrops), GetFrameTime());
        UpdateReferenceFolderTransfer(context, GetFrameTime());
        RpgReferenceObjects_UpdateFollowers(&(*context->referenceDrops), (*context->player).position,
                                            (*context->player).scale, (*context->player).moveSpeed,
                                            (*context->layout).referenceFollowerScale, GetFrameTime());
        for (int index = 0; index < (*context->items).count; index++) if (!(*context->items).entries[index].collected &&
            fabsf((*context->player).position.x - (*context->items).entries[index].position.x) <= 28.0f) {
            (*context->items).entries[index].collected = true;
            snprintf(itemMessage, (size_t)context->itemMessageSize, "%s を手に入れた", (*context->items).entries[index].name);
            GameFont_AddText(itemMessage);
            (*context->itemMessageTimer) = 2.5f;
        }
        for (int index = 0; index < (*context->events).count && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen); index++) if (!(*context->events).entries[index].triggered &&
            Vector2Distance((*context->player).position, (*context->events).entries[index].position) <= 36.0f) {
            (*context->events).entries[index].triggered = true;
            // 位置イベントは既存の調べるFunction列を起動し、会話・移動を同じ実装で再利用する。
            (*context->inspectTarget) = 1;
            (*context->inspectFunctionIndex) = 0;
            (*context->inspectLineIndex) = 0;
        }
        // 調べる機能列のMoveは、対象を指定時間で補間して完了後に次の機能へ進める。
        /* 旧Context互換用。現行Contextは下の独立Move更新を使う。 */
        /* 旧単一Move実装は複数Move実装と競合するため無効化する。 */
#if 0
        if (context->activeInspectMove == NULL && (*context->inspectTarget) >= 0) {
            RpgInspect *activeInspect = GetRuntimeActiveInspect(context);
            if (activeInspect->functions[(*context->inspectFunctionIndex)].type == RPG_INSPECT_MOVE) {
                RpgInspectMove *move = &activeInspect->functions[(*context->inspectFunctionIndex)].move;
                int imageIndex = move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT ?
                                 RpgImageObjects_FindById(&(*context->stage).imageObjects, move->targetImageObjectId) : -1;
                RpgImageObject *imageTarget = imageIndex >= 0 ? &(*context->stage).imageObjects.entries[imageIndex] : NULL;
                if (move->target == RPG_INSPECT_MOVE_IMAGE_OBJECT && imageTarget == NULL)
                    move->target = RPG_INSPECT_MOVE_PLAYER;
                if (!(*context->isInspectMoveRunning)) {
                    (*context->isInspectMoveRunning) = true;
                    (*context->inspectMoveElapsed) = 0.0f;
                    (*context->inspectMoveStartX) = imageTarget != NULL ?
                                        RpgImageObjects_GetWorldCenterX(imageTarget, RPG_STAGE_TILE_SIZE) :
                                        move->target == RPG_INSPECT_MOVE_PLAYER ? (*context->player).position.x :
                                        move->target == RPG_INSPECT_MOVE_NPC ? (*context->npc).position.x : (*context->zipper).character.position.x;
                    (*context->inspectMoveStartY) = imageTarget != NULL ?
                                        RpgImageObjects_GetWorldCenterY(imageTarget, RPG_STAGE_TILE_SIZE) :
                                        move->target == RPG_INSPECT_MOVE_PLAYER ? (*context->player).position.y :
                                        move->target == RPG_INSPECT_MOVE_NPC ? (*context->npc).position.y : (*context->zipper).character.position.y;
                }
                (*context->inspectMoveElapsed) += GetFrameTime();
                float progress = Clamp((*context->inspectMoveElapsed) / move->duration, 0.0f, 1.0f);
                float easedProgress = RpgInspect_EaseMoveProgress(move->easing, progress);
                float currentX = RpgInspect_MoveAxisHasX(move->axis) ?
                    (*context->inspectMoveStartX) + (move->destinationX - (*context->inspectMoveStartX)) * easedProgress :
                    (*context->inspectMoveStartX);
                float currentY = RpgInspect_MoveAxisHasY(move->axis) ?
                    (*context->inspectMoveStartY) + (move->destinationY - (*context->inspectMoveStartY)) * easedProgress :
                    (*context->inspectMoveStartY);
                if (imageTarget != NULL) RpgImageObjects_SetRuntimePosition(imageTarget, (Vector2){ currentX, currentY });
                else {
                    float *targetX = move->target == RPG_INSPECT_MOVE_PLAYER ? &(*context->player).position.x :
                                     move->target == RPG_INSPECT_MOVE_NPC ? &(*context->npc).position.x : &(*context->zipper).character.position.x;
                    float *targetY = move->target == RPG_INSPECT_MOVE_PLAYER ? &(*context->player).position.y :
                                     move->target == RPG_INSPECT_MOVE_NPC ? &(*context->npc).position.y : &(*context->zipper).character.position.y;
                    *targetX = currentX;
                    *targetY = currentY;
                }
                if (progress >= 1.0f) {
                    if (imageTarget != NULL)
                        RpgImageObjects_CommitRuntimePosition(imageTarget, RPG_STAGE_TILE_SIZE, RPG_STAGE_WORLD_COLUMNS,
                                                              RPG_STAGE_ROWS);
                    (*context->isInspectMoveRunning) = false;
                    (*context->inspectFunctionIndex)++;
                    if ((*context->inspectFunctionIndex) >= activeInspect->functionCount)
                        CompleteRuntimeInspect(context);
                    else (*context->inspectLineIndex) = 0;
                }
            }
        }
 #endif
        /* MoveはFunction列から独立して進めるため、次のFunctionへ遷移しても止まらない。 */
        UpdateRuntimeInspectMove(context, GetFrameTime());
#if 0
        if ((*context->inspectTarget) >= 0) {
            RpgInspect *activeInspect = GetRuntimeActiveInspect(context);
            if ((*context->inspectFunctionIndex) < 0 ||
                (*context->inspectFunctionIndex) >= activeInspect->functionCount) {
                /* 空のFunction列や終端でも、必ず共通の完了処理を通して入力を返す。 */
                CompleteRuntimeInspect(context);
            } else {
            RpgInspectFunction *function = &activeInspect->functions[(*context->inspectFunctionIndex)];
            if (function->type == RPG_INSPECT_MOVE) {
                RpgInspectMove *move = &function->move;
                StartRuntimeInspectMove(context, move);
                if (!(*context->isInspectMoveRunning) && *context->activeInspectMove == move) {
                    *context->inspectMoveTransitionElapsed += GetFrameTime();
                    if (*context->inspectMoveTransitionElapsed >= move->nextFunctionDelay) {
                        (*context->inspectFunctionIndex)++;
                        (*context->inspectLineIndex) = 0;
                        *context->activeInspectMove = NULL;
                        *context->inspectMoveTransitionElapsed = 0.0f;
                        if ((*context->inspectFunctionIndex) >= activeInspect->functionCount)
                            CompleteRuntimeInspect(context);
                    }
                }
            } else if (function->type == RPG_INSPECT_WAIT && context->activeWaitFunctionIndex != NULL &&
                       context->inspectWaitElapsed != NULL) {
                if (*context->activeWaitFunctionIndex != *context->inspectFunctionIndex) {
                    *context->activeWaitFunctionIndex = *context->inspectFunctionIndex;
                    *context->inspectWaitElapsed = 0.0f;
                }
                *context->inspectWaitElapsed += GetFrameTime();
                if (*context->inspectWaitElapsed >= function->wait.duration) {
                    *context->activeWaitFunctionIndex = -1;
                    *context->inspectWaitElapsed = 0.0f;
                    (*context->inspectFunctionIndex)++;
                    (*context->inspectLineIndex) = 0;
                    if ((*context->inspectFunctionIndex) >= activeInspect->functionCount)
                        CompleteRuntimeInspect(context);
                }
            } else if (function->type == RPG_INSPECT_LAYER_CHANGE) {
                int imageIndex = RpgImageObjects_FindById(&context->stage->imageObjects,
                                                          function->layerChange.targetImageObjectId);
                if (imageIndex >= 0)
                    context->stage->imageObjects.entries[imageIndex].layer =
                        Clamp(function->layerChange.layer, 0, 2);
                (*context->inspectFunctionIndex)++;
                (*context->inspectLineIndex) = 0;
                if ((*context->inspectFunctionIndex) >= activeInspect->functionCount)
                    CompleteRuntimeInspect(context);
            }
            }
        }
#endif
        AdvanceRuntimeInspectFunctions(context, GetFrameTime());
        currentMapIndex = GetRuntimeMapIndex(&(*context->stage), (*context->player).position, currentMapIndex);
        int areaIndex = currentMapIndex;
        /* エリアイベントは、そのエリアに初めて入った瞬間だけ既存の会話表示で実行する。 */
        if ((*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 &&
            (*context->stage3IntroIndex) < 0 && context->areaEntryEvents != NULL &&
            context->areaEntryShown != NULL && areaIndex >= 0 && areaIndex < RPG_STAGE_MAP_COUNT &&
            (*context->stage).mapActive[areaIndex] && !context->areaEntryShown[areaIndex]) {
            RpgStage3Event *areaEvent = &context->areaEntryEvents->entries[areaIndex];
            context->areaEntryShown[areaIndex] = true;
            if (areaEvent->inspect.enabled && areaEvent->inspect.functionCount > 0) {
                if (context->activeEntryEvent != NULL) (*context->activeEntryEvent) = areaEvent;
                (*context->inspectTarget) = 3;
                (*context->inspectFunctionIndex) = 0;
                (*context->inspectLineIndex) = 0;
            }
        }
        (*context->previousMap) = currentMapIndex;
        bool didTogglePushBlock = IsKeyPressed(KEY_G) && (*context->inspectTarget) < 0 &&
                                  (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0 &&
                                  !(*context->isReferenceTextOpen) &&
                                  RpgMagnets_TogglePlayerPush(context->magnetRuntime, &(*context->stage),
                                                              &playerPushState, (*context->player).position, 72.0f);
        bool isReferenceCollected = !didTogglePushBlock && canReadReference && !referenceFolderTransfer.active &&
                                   !IsReferenceFolderTarget(&(*context->stage), nearbyReferenceTarget) &&
                                   IsKeyPressed(KEY_G) &&
                                   (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 &&
                                   (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen);
        if (isReferenceCollected && RpgReferenceObjects_CollectTarget(&(*context->stage),
                                                                       &(*context->referenceDrops),
                                                                       nearbyReferenceTarget)) {
            snprintf(itemMessage, (size_t)context->itemMessageSize, "File acquired: %s",
                     context->referenceFileName);
            GameFont_AddText(itemMessage);
            (*context->itemMessageTimer) = 2.0f;
            (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
            (*context->isReferencePointerFeedbackSuppressed) = true;
            canReadReference = false;
            nearbyReferenceTarget.kind = RPG_REFERENCE_TARGET_NONE;
        }
        if (canStoreReference && !referenceFolderTransfer.active && IsKeyPressed(KEY_P) &&
            (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 &&
            (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen) &&
            StartReferenceFolderTransfer(context, nearbyFolderTarget)) {
            (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
            (*context->isReferencePointerFeedbackSuppressed) = true;
        }
        bool isReferenceCloseClicked = (*context->isReferenceTextOpen) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                       CheckCollisionPointRec(RpgViewport_GetMousePosition(), referenceTextCloseButton);
        if (isReferenceCloseClicked) {
            (*context->isReferenceTextOpen) = false;
        } else if (IsKeyPressed(KEY_E)) {
            if ((*context->isReferenceTextOpen)) {
                // ファイル表示も会話と同じく、Eで閉じるまでプレイヤー操作を止める。
                (*context->isReferenceTextOpen) = false;
            } else if ((*context->inspectTarget) >= 0) {
                const RpgInspect *activeInspect = GetRuntimeActiveInspect(context);
                if ((*context->inspectFunctionIndex) < 0 ||
                    (*context->inspectFunctionIndex) >= activeInspect->functionCount) {
                    CompleteRuntimeInspect(context);
                } else if (activeInspect->functions[(*context->inspectFunctionIndex)].type == RPG_INSPECT_DIALOGUE) {
                    (*context->inspectLineIndex)++;
                    if ((*context->inspectLineIndex) >= activeInspect->functions[(*context->inspectFunctionIndex)].dialogue.lineCount) {
                        (*context->inspectFunctionIndex)++;
                        (*context->inspectLineIndex) = 0;
                        if ((*context->inspectFunctionIndex) >= activeInspect->functionCount)
                            CompleteRuntimeInspect(context);
                    }
                }
            } else
            if ((*context->dialogueIndex) >= 0) {
                (*context->dialogueIndex)++;
                if ((*context->dialogueIndex) >= (*context->dialogue).lineCount) (*context->dialogueIndex) = -1;
            } else if (canTalk) {
                (*context->dialogueIndex) = 0;
            } else if (nearbyKeyDoor.row >= 0 && !referenceFolderTransfer.active) {
                int followerIndex = RpgReferenceObjects_FindFollowerIndex(&(*context->referenceDrops));
                RpgKeyDoor *door = RpgStage_GetKeyDoorAtCell(&(*context->stage), nearbyKeyDoor.row, nearbyKeyDoor.column);
                if (followerIndex >= 0 && StartKeyDoorTransfer(context, nearbyKeyDoor)) {
                    (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
                    (*context->isReferencePointerFeedbackSuppressed) = true;
                } else if (door != NULL) {
                    snprintf(itemMessage, (size_t)context->itemMessageSize, "%s",
                             door->failureText[0] != '\0' ? door->failureText : "This door needs its key file.");
                    GameFont_AddText(itemMessage);
                    *context->itemMessageTimer = 2.0f;
                }
            } else if (canReadReference) {
                const char *referencePath = RpgReferenceObjects_GetTargetPath(&(*context->stage), &(*context->referenceDrops),
                                                                                nearbyReferenceTarget);
                if (IsReferenceFolderTarget(&(*context->stage), nearbyReferenceTarget))
                    RpgExplorerLauncher_OpenDirectory(referencePath);
                else OpenTextFile(referencePath, context->referenceFileName, (size_t)context->referenceFileNameSize,
                                  context->referenceText, (size_t)context->referenceTextSize,
                                  &(*context->isReferenceTextOpen));
                (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
                (*context->isReferencePointerFeedbackSuppressed) = true;
            }
        }
        /* NPC と Zipper の個別「調べる」は廃止した。Eの通常会話とイベントFunction列は別経路で維持する。 */
        /* 入力・エリア侵入で開始または更新された列を同一フレームで実行する。
           時間を設定したFunctionだけが次フレーム以降へ継続する。 */
        AdvanceRuntimeInspectFunctions(context, 0.0f);
        UpdateRpgCamera(&(*context->camera), &(*context->stage), currentMapIndex,
                        (*context->player).position, (*context->cameraFollowsPlayer));
        RpgReferenceTarget hoveredReferencePointerTarget = { .kind = RPG_REFERENCE_TARGET_NONE,
                                                               .row = -1, .column = -1, .dropIndex = -1 };
        bool isReferencePointerHovered = false;
        if (!(*context->isReferenceTextOpen) && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 && (*context->stage3IntroIndex) < 0) {
            Vector2 pointerWorldPosition = GetRuntimePointerWorldPosition((*context->camera), &(*context->stage), currentMapIndex);
            isReferencePointerHovered = !(*context->isReferenceDragActive) &&
                                        RpgReferenceObjects_FindTarget(&(*context->stage), &(*context->referenceDrops),
                                                                       pointerWorldPosition,
                                                                       &hoveredReferencePointerTarget);
            if (!isReferencePointerHovered) (*context->isReferencePointerFeedbackSuppressed) = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (isReferencePointerHovered) {
                    if ((*context->lastReferencePointerClickTime) >= 0.0 &&
                        GetTime() - (*context->lastReferencePointerClickTime) <= 0.35) {
                        const char *referencePath = RpgReferenceObjects_GetTargetPath(&(*context->stage), &(*context->referenceDrops),
                                                                                        hoveredReferencePointerTarget);
                        if (IsReferenceFolderTarget(&(*context->stage), hoveredReferencePointerTarget))
                            RpgExplorerLauncher_OpenDirectory(referencePath);
                        else OpenTextFile(referencePath, context->referenceFileName,
                                          (size_t)context->referenceFileNameSize, context->referenceText,
                                          (size_t)context->referenceTextSize, &(*context->isReferenceTextOpen));
                        (*context->isReferencePointerPressed) = false;
                        (*context->lastReferencePointerClickTime) = -1.0;
                    } else {
                        (*context->selectedReferencePointerTarget) = hoveredReferencePointerTarget;
                        (*context->isReferencePointerFeedbackSuppressed) = false;
                        (*context->isReferencePointerPressed) = true;
                        (*context->pressedReferenceTarget) = hoveredReferencePointerTarget;
                        (*context->referencePressPosition) = pointerWorldPosition;
                        (*context->lastReferencePointerClickTime) = GetTime();
                    }
                } else {
                    (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
                    (*context->isReferencePointerFeedbackSuppressed) = true;
                }
            }
            if ((*context->isReferencePointerPressed) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                Vector2Distance((*context->referencePressPosition), pointerWorldPosition) > 6.0f) {
                // クリックとドラッグを距離で分離し、ドラッグ中だけ元のマスから非表示にする。
                (*context->isReferenceDragActive) = true;
                (*context->draggedReferenceTarget) = (*context->pressedReferenceTarget);
                (*context->isReferencePointerPressed) = false;
                (*context->selectedReferencePointerTarget).kind = RPG_REFERENCE_TARGET_NONE;
                (*context->isReferencePointerFeedbackSuppressed) = true;
            }
            if ((*context->isReferenceDragActive)) {
                (*context->referenceDragPosition) = pointerWorldPosition;
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    Rectangle zipperBounds = RpgZipper_GetSpriteBounds(&(*context->zipper).character, 380.0f);
                    if (CheckCollisionPointRec(pointerWorldPosition, zipperBounds)) {
                        char zipperInbox[RPG_STAGE_REFERENCE_PATH_LENGTH] = { 0 };
                        if (RpgObjectFolder_GetZipperInboxDirectory(zipperInbox, sizeof(zipperInbox)) &&
                            StoreReferenceTargetInDirectory(context, (*context->draggedReferenceTarget),
                                                            zipperInbox, "File stored in Zipper")) {
                            /* 移動成功後の演出だけを入力方法ごとに足し、実ファイル操作は共通化する。 */
                            StartZipperImportAnimation(context->zipperAnimationElapsed);
                        }
                    }
                    (*context->isReferenceDragActive) = false;
                    (*context->draggedReferenceTarget).kind = RPG_REFERENCE_TARGET_NONE;
                }
            }
            if ((*context->isReferencePointerPressed) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                (*context->isReferencePointerPressed) = false;
        }
        bool isZipperPointerHovered = false;
        if (((*context->zipperFollowsPlayer) || (*context->isZipperAttachedToBlock) || (*context->attachedDataShotIndex) >= 0) && (*context->inspectTarget) < 0 && (*context->dialogueIndex) < 0 &&
            (*context->stage3IntroIndex) < 0 && !(*context->isReferenceTextOpen)) {
            Vector2 pointerWorldPosition = GetRuntimePointerWorldPosition((*context->camera), &(*context->stage), currentMapIndex);
            Rectangle zipperBounds = RpgZipper_GetSpriteBounds(&(*context->zipper).character, 380.0f);
            isZipperPointerHovered = CheckCollisionPointRec(pointerWorldPosition, zipperBounds);
            if (!isZipperPointerHovered) (*context->isZipperPointerFeedbackSuppressed) = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (isZipperPointerHovered && GetTime() - (*context->lastZipperPointerClickTime) <= 0.35) {
                    RpgObjectFolder_OpenZipperDirectory();
                    (*context->lastZipperPointerClickTime) = -1.0;
                } else if (isZipperPointerHovered) {
                    (*context->lastZipperPointerClickTime) = GetTime();
                    (*context->isZipperPointerFeedbackSuppressed) = false;
                }
                else {
                    zipperPointerSelected = false;
                    (*context->isZipperPointerFeedbackSuppressed) = true;
                }
                if (isZipperPointerHovered) zipperPointerSelected = true;
            }
        } else {
            zipperPointerSelected = false;
        }
        const RpgStage3Event *activeEntryEvent = context->stage3Event;
        if (context->activeEntryEvent != NULL && (*context->activeEntryEvent) != NULL)
            activeEntryEvent = *context->activeEntryEvent;
        RpgCharacterAnimation playerAnimation = RPG_CHARACTER_ANIMATION_AUTOMATIC;
        if (HasRunningPlayerWalkMove())
            playerAnimation = RPG_CHARACTER_ANIMATION_WALK;
        DrawRpgWorld(&(*context->player), &(*context->npc), &(*context->stage), context->magnetRuntime, currentMapIndex,
                     (*context->camera), canTalk,
                     &(*context->dialogue), (*context->dialogueIndex), (*context->stage3IntroIndex), activeEntryEvent, &(*context->zipper), zipperTexture, fileTexture, &(*context->inspect), &(*context->zipper).inspect,
                     (*context->inspectTarget), (*context->inspectFunctionIndex), (*context->inspectLineIndex), false, 0.0f, RPG_INSPECT_MOVE_PLAYER,
                     (*context->npcInspectCompleted), (*context->zipperInspectCompleted),
                     isZipperPointerHovered && !(*context->isZipperPointerFeedbackSuppressed), zipperPointerSelected,
                     &(*context->items), &(*context->referenceDrops), &(*context->wires), &(*context->receivers), &(*context->attachments), &(*context->dataShots), &(*context->events),
                     itemMessage, (*context->itemMessageTimer), nearbyReferenceTarget,
                     context->referenceFileName, context->referenceText, (*context->isReferenceTextOpen),
                     hoveredReferencePointerTarget, (*context->selectedReferencePointerTarget),
                     (*context->isReferencePointerFeedbackSuppressed), (*context->isReferenceDragActive),
                     (*context->draggedReferenceTarget), (*context->referenceDragPosition),
                     (*context->zipperAnimationElapsed), context->stageBackground,
                     context->layout->backgroundBrightness, context->layout->blockBrightness,
                      (*context->isZipperLaunched), playerAnimation, context->showStopButton, context->scene,
                      context->layout->zipperMaxCapacityKB, (*context->isZipperControllable));
    }

#undef fileTexture
#undef zipperTexture
#undef itemMessage
#undef zipperPointerSelected
}
