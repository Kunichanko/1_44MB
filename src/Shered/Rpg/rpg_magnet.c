// 役割: 磁石の磁場と、金属ブロックを実行時だけ連続移動する可動固体として管理する。
// 依存する自プロジェクト内ファイル: rpg_magnet.h, rpg_block_inventory.h, rpg_stage.h
#include "rpg_magnet.h"

#include <math.h>
#include <stddef.h>

#include "raymath.h"
#include "rpg_block_inventory.h"

enum { RPG_MAGNET_DIRECTION_COUNT = 4 };

static const int magnetDirectionRows[RPG_MAGNET_DIRECTION_COUNT] = { -1, 1, 0, 0 };
static const int magnetDirectionColumns[RPG_MAGNET_DIRECTION_COUNT] = { 0, 0, -1, 1 };

RpgMagnetRuntime RpgMagnetRuntime_Default(void)
{
    return (RpgMagnetRuntime){ 0 };
}

RpgPlayerPushState RpgPlayerPushState_Default(void)
{
    return (RpgPlayerPushState){ .heldBlockIndex = -1 };
}

bool RpgMagnets_ToggleAtCell(RpgStage *stage, int row, int column)
{
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
        column >= RPG_STAGE_WORLD_COLUMNS) return false;
    int *blockType = &stage->blocks[row][column];
    if (*blockType == RPG_BLOCK_EFFECT_MAGNET_OFF) {
        *blockType = RPG_BLOCK_EFFECT_MAGNET_ON;
        return true;
    }
    if (*blockType == RPG_BLOCK_EFFECT_MAGNET_ON) {
        *blockType = RPG_BLOCK_EFFECT_MAGNET_OFF;
        return true;
    }
    return false;
}

static bool IsCellInsideStage(int row, int column)
{
    return row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS;
}

static Rectangle GetMetalBounds(Vector2 position)
{
    return (Rectangle){ position.x, position.y, RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
}

void RpgMagnets_InitializeForStage(RpgMagnetRuntime *runtime, RpgStage *stage)
{
    if (runtime == NULL || stage == NULL || runtime->isInitialized) return;
    runtime->metalCount = 0;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            int blockType = stage->blocks[row][column];
            if (!RpgBlockInventory_IsMetalBlock(blockType) &&
                !RpgBlockInventory_IsPushBlock(blockType)) continue;
            if (runtime->metalCount >= RPG_MAGNET_MAX_METALS) continue;
            RpgMagnetMetal *metal = &runtime->metals[runtime->metalCount++];
            Rectangle worldCell = RpgStage_GetWorldBoundsForCell(stage, row, column);
            metal->position = (Vector2){ worldCell.x, worldCell.y };
            metal->previousPosition = metal->position;
            metal->blockType = blockType;
            metal->active = true;
            runtime->movingSolids[runtime->metalCount - 1] = (RpgMovingSolid){
                .previousBounds = GetMetalBounds(metal->previousPosition),
                .bounds = GetMetalBounds(metal->position)
            };
            /* 保存用グリッドから外し、以後の衝突・描画は可動固体を参照する。 */
            stage->blocks[row][column] = 0;
        }
    }
    runtime->isInitialized = true;
}

void RpgMagnets_BeginFrame(RpgMagnetRuntime *runtime)
{
    if (runtime == NULL || !runtime->isInitialized) return;
    for (int index = 0; index < runtime->metalCount; index++)
        if (runtime->metals[index].active)
            runtime->metals[index].previousPosition = runtime->metals[index].position;
}

static bool DoesMetalCollide(const RpgMagnetRuntime *runtime, int ignoredIndex, Rectangle bounds)
{
    for (int index = 0; index < runtime->metalCount; index++) {
        if (index == ignoredIndex || !runtime->metals[index].active) continue;
        if (CheckCollisionRecs(bounds, GetMetalBounds(runtime->metals[index].position))) return true;
    }
    return false;
}

static bool IsRayClear(const RpgStage *stage, int magnetRow, int magnetColumn,
                       int directionRow, int directionColumn, Vector2 metalPosition)
{
    int metalRow;
    int metalColumn;
    Vector2 magnetCenter = RpgStage_GetWorldPositionForCell(stage, magnetRow, magnetColumn);
    if (!RpgStage_GetWorldCellAtPosition(stage, (Vector2){ metalPosition.x + RPG_STAGE_TILE_SIZE * 0.5f,
                                                            metalPosition.y + RPG_STAGE_TILE_SIZE * 0.5f },
                                        &metalRow, &metalColumn)) return false;
    for (int distance = 1;; distance++) {
        Vector2 sample = { magnetCenter.x + directionColumn * distance * RPG_STAGE_TILE_SIZE,
                           magnetCenter.y + directionRow * distance * RPG_STAGE_TILE_SIZE };
        int row;
        int column;
        if (!RpgStage_GetWorldCellAtPosition(stage, sample, &row, &column)) return false;
        if (row == metalRow && column == metalColumn) return true;
        if (stage->blocks[row][column] != 0) return false;
    }
}

static bool FindMagnetTarget(const RpgMagnetRuntime *runtime, const RpgStage *stage,
                             int metalIndex, Vector2 *target)
{
    const RpgMagnetMetal *metal = &runtime->metals[metalIndex];
    if (!RpgBlockInventory_IsMetalBlock(metal->blockType)) return false;
    Vector2 metalCenter = { metal->position.x + RPG_STAGE_TILE_SIZE * 0.5f,
                            metal->position.y + RPG_STAGE_TILE_SIZE * 0.5f };
    float bestDistance = 0.0f;
    bool found = false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            if (!RpgBlockInventory_IsMagnetActive(stage->blocks[row][column])) continue;
            Vector2 magnetCenter = RpgStage_GetWorldPositionForCell(stage, row, column);
            for (int direction = 0; direction < RPG_MAGNET_DIRECTION_COUNT; direction++) {
                int directionRow = magnetDirectionRows[direction];
                int directionColumn = magnetDirectionColumns[direction];
                bool aligned = directionRow != 0 ? fabsf(metalCenter.x - magnetCenter.x) < 0.5f :
                                                    fabsf(metalCenter.y - magnetCenter.y) < 0.5f;
                float signedDistance = directionRow != 0 ? (metalCenter.y - magnetCenter.y) * directionRow :
                                                           (metalCenter.x - magnetCenter.x) * directionColumn;
                if (!aligned || signedDistance < RPG_STAGE_TILE_SIZE - 0.5f ||
                    !IsRayClear(stage, row, column, directionRow, directionColumn, metal->position)) continue;
                Vector2 candidate = { magnetCenter.x - RPG_STAGE_TILE_SIZE * 0.5f + directionColumn * RPG_STAGE_TILE_SIZE,
                                      magnetCenter.y - RPG_STAGE_TILE_SIZE * 0.5f + directionRow * RPG_STAGE_TILE_SIZE };
                if (DoesMetalCollide(runtime, metalIndex, GetMetalBounds(candidate))) continue;
                if (!found || signedDistance < bestDistance) {
                    *target = candidate;
                    bestDistance = signedDistance;
                    found = true;
                }
            }
        }
    }
    return found;
}

static float MoveMetalAxis(RpgMagnetRuntime *runtime, const RpgStage *stage, int metalIndex,
                           float amount, bool vertical)
{
    RpgMagnetMetal *metal = &runtime->metals[metalIndex];
    float start = vertical ? metal->position.y : metal->position.x;
    float remaining = fabsf(amount);
    float direction = amount < 0.0f ? -1.0f : 1.0f;
    while (remaining > 0.0f) {
        float step = direction * fminf(remaining, 2.0f);
        Vector2 candidate = metal->position;
        if (vertical) candidate.y += step;
        else candidate.x += step;
        Rectangle candidateBounds = GetMetalBounds(candidate);
        if (RpgStage_CheckSolidCollision(stage, candidateBounds) ||
            DoesMetalCollide(runtime, metalIndex, candidateBounds)) break;
        metal->position = candidate;
        remaining -= fabsf(step);
    }
    return (vertical ? metal->position.y : metal->position.x) - start;
}

static void UpdateMetal(RpgMagnetRuntime *runtime, const RpgStage *stage, int metalIndex,
                        float pixelsPerSecond, float deltaTime)
{
    RpgMagnetMetal *metal = &runtime->metals[metalIndex];
    metal->previousPosition = metal->position;
    Vector2 magnetTarget = { 0 };
    if (FindMagnetTarget(runtime, stage, metalIndex, &magnetTarget)) {
        float dx = magnetTarget.x - metal->position.x;
        float dy = magnetTarget.y - metal->position.y;
        float distance = pixelsPerSecond * deltaTime;
        if (fabsf(dx) > 0.01f) MoveMetalAxis(runtime, stage, metalIndex,
                                              fmaxf(-distance, fminf(dx, distance)), false);
        else if (fabsf(dy) > 0.01f) MoveMetalAxis(runtime, stage, metalIndex,
                                                   fmaxf(-distance, fminf(dy, distance)), true);
        return;
    }
    /* 磁場に無い金属だけ重力で連続落下する。 */
    MoveMetalAxis(runtime, stage, metalIndex, pixelsPerSecond * deltaTime, true);
}

static void RefreshMovingSolids(RpgMagnetRuntime *runtime)
{
    for (int index = 0; index < runtime->metalCount; index++) {
        const RpgMagnetMetal *metal = &runtime->metals[index];
        runtime->movingSolids[index] = metal->active ? (RpgMovingSolid){
            .previousBounds = GetMetalBounds(metal->previousPosition),
            .bounds = GetMetalBounds(metal->position)
        } : (RpgMovingSolid){ 0 };
    }
}

void RpgMagnets_Update(RpgMagnetRuntime *runtime, RpgStage *stage, float pixelsPerSecond, float deltaTime,
                       const RpgPlayerPushState *pushState)
{
    if (runtime == NULL || stage == NULL || deltaTime <= 0.0f) return;
    RpgMagnets_InitializeForStage(runtime, stage);
    if (pixelsPerSecond < 1.0f) pixelsPerSecond = 1.0f;
    for (int index = 0; index < runtime->metalCount; index++)
        if (runtime->metals[index].active &&
            (pushState == NULL || pushState->heldBlockIndex != index))
            UpdateMetal(runtime, stage, index, pixelsPerSecond, deltaTime);
    RefreshMovingSolids(runtime);
}

RpgMovingSolidSet RpgMagnets_GetMovingSolids(const RpgMagnetRuntime *runtime)
{
    if (runtime == NULL || !runtime->isInitialized) return (RpgMovingSolidSet){ 0 };
    return (RpgMovingSolidSet){ .entries = runtime->movingSolids, .count = runtime->metalCount };
}

RpgMovingSolidSet RpgMagnets_GetMovingSolidsExcept(const RpgMagnetRuntime *runtime, int excludedIndex,
                                                    RpgMovingSolid *storage, int storageCapacity)
{
    if (runtime == NULL || !runtime->isInitialized || storage == NULL || storageCapacity < 1)
        return (RpgMovingSolidSet){ 0 };
    int count = 0;
    for (int index = 0; index < runtime->metalCount && count < storageCapacity; index++) {
        if (index == excludedIndex || !runtime->metals[index].active) continue;
        storage[count++] = runtime->movingSolids[index];
    }
    return (RpgMovingSolidSet){ .entries = storage, .count = count };
}

bool RpgMagnets_IsPlayerPushHeld(const RpgMagnetRuntime *runtime,
                                 const RpgPlayerPushState *pushState)
{
    return runtime != NULL && pushState != NULL && pushState->heldBlockIndex >= 0 &&
           pushState->heldBlockIndex < runtime->metalCount &&
           runtime->metals[pushState->heldBlockIndex].active &&
           RpgBlockInventory_IsPushBlock(runtime->metals[pushState->heldBlockIndex].blockType);
}

float RpgMagnets_GetHeldPushBlockX(const RpgMagnetRuntime *runtime,
                                   const RpgPlayerPushState *pushState)
{
    return RpgMagnets_IsPlayerPushHeld(runtime, pushState) ?
        runtime->metals[pushState->heldBlockIndex].position.x : 0.0f;
}

bool RpgMagnets_TogglePlayerPush(RpgMagnetRuntime *runtime, RpgStage *stage,
                                 RpgPlayerPushState *pushState, Vector2 playerPosition,
                                 float maximumDistance)
{
    if (runtime == NULL || stage == NULL || pushState == NULL) return false;
    RpgMagnets_InitializeForStage(runtime, stage);
    if (RpgMagnets_IsPlayerPushHeld(runtime, pushState)) {
        *pushState = RpgPlayerPushState_Default();
        return true;
    }
    int closestIndex = -1;
    float closestDistance = maximumDistance;
    for (int index = 0; index < runtime->metalCount; index++) {
        const RpgMagnetMetal *block = &runtime->metals[index];
        if (!block->active || !RpgBlockInventory_IsPushBlock(block->blockType)) continue;
        Vector2 center = { block->position.x + RPG_STAGE_TILE_SIZE * 0.5f,
                           block->position.y + RPG_STAGE_TILE_SIZE * 0.5f };
        float distance = Vector2Distance(playerPosition, center);
        if (distance <= closestDistance) {
            closestDistance = distance;
            closestIndex = index;
        }
    }
    if (closestIndex < 0) return false;
    pushState->heldBlockIndex = closestIndex;
    pushState->playerToBlockOffsetX = runtime->metals[closestIndex].position.x - playerPosition.x;
    return true;
}

float RpgMagnets_MoveHeldPushBlock(RpgMagnetRuntime *runtime, const RpgStage *stage,
                                   const RpgPlayerPushState *pushState, float amountX)
{
    if (!RpgMagnets_IsPlayerPushHeld(runtime, pushState) || fabsf(amountX) < 0.001f) return 0.0f;
    int index = pushState->heldBlockIndex;
    runtime->metals[index].previousPosition = runtime->metals[index].position;
    return MoveMetalAxis(runtime, stage, index, amountX, false);
}

void RpgMagnets_TranslateHeldPushBlock(RpgMagnetRuntime *runtime,
                                       const RpgPlayerPushState *pushState, Vector2 delta)
{
    if (!RpgMagnets_IsPlayerPushHeld(runtime, pushState)) return;
    int index = pushState->heldBlockIndex;
    RpgMagnetMetal *block = &runtime->metals[index];
    block->position = Vector2Add(block->position, delta);
    block->previousPosition = block->position;
    /* This is a coordinate remap, not physical movement.  Matching both
       bounds prevents the player contact resolver from applying it as a
       sudden platform push. */
    runtime->movingSolids[index] = (RpgMovingSolid){
        .previousBounds = GetMetalBounds(block->position),
        .bounds = GetMetalBounds(block->position)
    };
}

void RpgMagnets_DrawMetals(const RpgMagnetRuntime *runtime, int firstColumn, int columnCount,
                           float worldOffsetX, float brightness)
{
    if (runtime == NULL || !runtime->isInitialized || columnCount <= 0) return;
    float left = firstColumn * RPG_STAGE_TILE_SIZE;
    float right = (firstColumn + columnCount) * RPG_STAGE_TILE_SIZE;
    for (int index = 0; index < runtime->metalCount; index++) {
        const RpgMagnetMetal *metal = &runtime->metals[index];
        if (!metal->active || metal->position.x + RPG_STAGE_TILE_SIZE <= left || metal->position.x >= right) continue;
        Rectangle bounds = GetMetalBounds(metal->position);
        bounds.x += worldOffsetX;
        RpgStage_DrawBlockCell(bounds, metal->blockType, brightness);
    }
}

void RpgMagnets_DrawFields(const RpgStage *stage, int firstColumn, int columnCount)
{
    if (stage == NULL || columnCount <= 0) return;
    int lastColumn = firstColumn + columnCount;
    if (firstColumn < 0) firstColumn = 0;
    if (lastColumn > RPG_STAGE_WORLD_COLUMNS) lastColumn = RPG_STAGE_WORLD_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = firstColumn; column < lastColumn; column++) {
            if (!RpgBlockInventory_IsMagnetActive(stage->blocks[row][column])) continue;
            Vector2 center = { column * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                               row * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f };
            for (int direction = 0; direction < RPG_MAGNET_DIRECTION_COUNT; direction++) {
                int directionRow = magnetDirectionRows[direction];
                int directionColumn = magnetDirectionColumns[direction];
                for (int distance = 1;; distance++) {
                    int targetRow = row + directionRow * distance;
                    int targetColumn = column + directionColumn * distance;
                    if (!IsCellInsideStage(targetRow, targetColumn)) break;
                    int blockType = stage->blocks[targetRow][targetColumn];
                    Vector2 target = { targetColumn * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                                       targetRow * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f };
                    DrawLineEx(center, target, 1.5f, Fade(SKYBLUE, 0.28f));
                    if (blockType != 0) break;
                }
            }
        }
    }
}
