// 役割: 磁石の磁場と、金属ブロックを実行時だけ連続移動する可動固体として管理する。
// 依存する自プロジェクト内ファイル: rpg_magnet.h, rpg_block_inventory.h, rpg_stage.h
#include "rpg_magnet.h"

#include <math.h>
#include <stddef.h>

#include "raymath.h"
#include "rpg_block_inventory.h"
#include "rpg_object_folder.h"
#include "rpg_physics.h"

enum { RPG_MAGNET_DIRECTION_COUNT = 4 };
static const float RPG_BLOCK_SOCKET_SETTLE_SPEED = 80.0f;

static const int magnetDirectionRows[RPG_MAGNET_DIRECTION_COUNT] = { -1, 1, 0, 0 };
static const int magnetDirectionColumns[RPG_MAGNET_DIRECTION_COUNT] = { 0, 0, -1, 1 };

/* A rectangular magnet emits from its exterior only.  Cell-only magnets from
 * an old stage remain compatible and behave as one-cell rectangles. */
static bool IsMagnetOuterEdgeDirection(const RpgStage *stage, int row, int column,
                                       int directionRow, int directionColumn)
{
    const RpgMagnetObject *magnet = RpgStage_GetMagnetAtCellConst(stage, row, column);
    if (magnet == NULL) return true;
    if (directionRow < 0) return row == magnet->rootRow;
    if (directionRow > 0) return row == magnet->rootRow + magnet->height - 1;
    if (directionColumn < 0) return column == magnet->rootColumn;
    return column == magnet->rootColumn + magnet->width - 1;
}

RpgMagnetRuntime RpgMagnetRuntime_Default(void)
{
    return (RpgMagnetRuntime){ 0 };
}

RpgPlayerPushState RpgPlayerPushState_Default(void)
{
    return (RpgPlayerPushState){ .heldBlockIndex = -1 };
}

static Vector2 GetPushBlockCenter(const RpgMagnetMetal *block)
{
    return (Vector2){ block->position.x + RPG_STAGE_TILE_SIZE * 0.5f,
                      block->position.y + RPG_STAGE_TILE_SIZE * 0.5f };
}

/* This is deliberately shared by G acquisition and carried-block release.
   A block must be reachable in the normal interaction radius and within 8px
   vertically; carrying cannot persist through a floor or ceiling. */
static bool IsWithinPushBlockInteractionRange(const RpgCharacter *player,
                                              const RpgMagnetMetal *block,
                                              float maximumDistance)
{
    Vector2 center = GetPushBlockCenter(block);
    /* Carry interaction uses the tile's stable logical centre, not the
       sprite-derived collision rectangle.  This keeps its vertical reach
       consistent across player sprite frames. */
    Vector2 playerCenter = { player->position.x,
                             player->position.y - RPG_STAGE_TILE_SIZE * 0.5f };
    return fabsf(center.y - playerCenter.y) <=
               (float)RPG_PUSH_BLOCK_INTERACTION_VERTICAL_RANGE &&
           Vector2Distance(playerCenter, center) <= maximumDistance;
}

void RpgMagnets_ConsumeCommunication(RpgMagnetRuntime *runtime, RpgStage *stage,
                                     const RpgButtonEvent *communication)
{
    if (runtime == NULL || stage == NULL || communication == NULL ||
        !RpgButtonEvent_Consume(communication, &runtime->lastCommunicationSequence) ||
        communication->sourceMapIndex < 0) return;

    /* Communication is area-scoped, like the other signal consumers.  It is
       intentionally unrelated to receiver-owned electrical wire paths. */
    for (int index = 0; index < stage->magnetCount; index++) {
        RpgMagnetObject *magnet = &stage->magnets[index];
        /* A Zipper-held composite keeps its geometry record so it can be
           spat back out, but it has no world cells and must not reactivate
           itself when a communication packet arrives. */
        if (stage->blocks[magnet->rootRow][magnet->rootColumn] == RPG_BLOCK_BUILD_MISSING)
            continue;
        Vector2 center = RpgStage_GetWorldPositionForCell(stage, magnet->rootRow, magnet->rootColumn);
        if (RpgStage_GetMapAtWorldPosition(stage, center) != communication->sourceMapIndex) continue;
        magnet->blockType = communication->isActive ? RPG_BLOCK_EFFECT_MAGNET_ON :
                                                       RPG_BLOCK_EFFECT_MAGNET_OFF;
        for (int row = magnet->rootRow; row < magnet->rootRow + magnet->height; row++)
            for (int column = magnet->rootColumn; column < magnet->rootColumn + magnet->width; column++)
                stage->blocks[row][column] = magnet->blockType;
    }
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
            metal->verticalSpeed = 0.0f;
            metal->isGrounded = false;
            metal->lockedSocketAttachmentIndex = -1;
            metal->socketSettleOffset = 0.0f;
            metal->requiresSocketDeparture = false;
            metal->objectCell = (RpgGridCell){ row, column };
            metal->blockType = blockType;
            metal->active = true;
            runtime->movingSolids[runtime->metalCount - 1] = (RpgMovingSolid){
                .previousBounds = GetMetalBounds(metal->previousPosition),
                .bounds = GetMetalBounds(metal->position)
            };
            /* 保存用グリッドから外し、以後の衝突・描画は可動固体を参照する。 */
            stage->blocks[row][column] = 0;
            /* Dynamic blocks are not terrain cells: give each its own object
               folder while the runtime build is active. */
            (void)RpgObjectFolder_EnsureDynamicBlock(metal->objectCell, metal->blockType,
                                                      metal->position);
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

void RpgMagnets_LockInBlockSockets(RpgMagnetRuntime *runtime, RpgStage *stage,
                                   const RpgAttachments *attachments, RpgButtonEvent *event,
                                   RpgPlayerPushState *pushState)
{
    (void)event;
    if (runtime == NULL || stage == NULL || attachments == NULL || !runtime->isInitialized)
        return;
    for (int index = 0; index < runtime->metalCount; index++) {
        RpgMagnetMetal *metal = &runtime->metals[index];
        int socketIndex;
        if (!metal->active || metal->lockedSocketAttachmentIndex >= 0) continue;
        socketIndex = RpgAttachments_FindBlockSocketAtBoundsWorld(attachments, stage,
                                                                   GetMetalBounds(metal->position));
        if (socketIndex < 0) {
            metal->requiresSocketDeparture = false;
            continue;
        }
        if (metal->requiresSocketDeparture) continue;
        /* The test intentionally accepts a small physics tolerance.  Once a
           block is accepted, snap it to the actual target cell before its
           4px seating animation so its collision, signal, and light occlusion
           all agree on one exact socket position. */
        {
            RpgGridCell socketCell = RpgGridPath_GetSideNeighbor(
                attachments->entries[socketIndex].cell, attachments->entries[socketIndex].side);
            Rectangle socketBounds = RpgStage_GetWorldBoundsForCell(stage, socketCell.row,
                                                                     socketCell.column);
            metal->position = (Vector2){ socketBounds.x, socketBounds.y };
        }
        metal->lockedSocketAttachmentIndex = socketIndex;
        metal->verticalSpeed = 0.0f;
        metal->isGrounded = true;
        metal->previousPosition = metal->position;
        metal->socketSettleOffset = 0.0f;
        /* Releasing here makes a carried block become fixed the instant it
           reaches a fully covered socket, without requiring another G press. */
        if (pushState != NULL && pushState->heldBlockIndex == index)
            *pushState = RpgPlayerPushState_Default();
    }
}

void RpgMagnets_UpdateBlockSocketSignals(const RpgMagnetRuntime *runtime, RpgStage *stage,
                                         const RpgAttachments *attachments,
                                         RpgButtonEvent *event)
{
    bool hasCoveredSocket[RPG_STAGE_MAP_COUNT] = { false };
    if (runtime == NULL || stage == NULL || attachments == NULL || event == NULL ||
        !runtime->isInitialized) return;

    for (int index = 0; index < runtime->metalCount; index++) {
        const RpgMagnetMetal *metal = &runtime->metals[index];
        int socketIndex;
        int mapIndex;
        Vector2 center;
        if (!metal->active) continue;
        /* This full-bounds test is also the socket beam's occlusion rule: a
           partial overlap never emits, while a completely covered aperture
           emits whether or not the block is currently fixed. */
        socketIndex = RpgAttachments_FindBlockSocketAtBoundsWorld(
            attachments, stage, GetMetalBounds(metal->position));
        if (socketIndex < 0) continue;
        center = (Vector2){ metal->position.x + RPG_STAGE_TILE_SIZE * 0.5f,
                            metal->position.y + RPG_STAGE_TILE_SIZE * 0.5f };
        mapIndex = RpgStage_GetMapAtWorldPosition(stage, center);
        if (mapIndex < 0)
            mapIndex = attachments->entries[socketIndex].cell.column / RPG_STAGE_COLUMNS;
        if (mapIndex >= 0 && mapIndex < RPG_STAGE_MAP_COUNT)
            hasCoveredSocket[mapIndex] = true;
    }

    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++) {
        if (RpgStage_IsSocketSignalActive(stage, mapIndex) == hasCoveredSocket[mapIndex]) continue;
        RpgStage_SetSocketSignalActive(stage, mapIndex, hasCoveredSocket[mapIndex]);
        RpgButtonEvent_PublishBlockSocket(event, mapIndex, hasCoveredSocket[mapIndex]);
    }
}

static bool DoesMetalCollide(const RpgMagnetRuntime *runtime, int ignoredIndex, Rectangle bounds)
{
    for (int index = 0; index < runtime->metalCount; index++) {
        if (index == ignoredIndex || !runtime->metals[index].active) continue;
        if (CheckCollisionRecs(bounds, GetMetalBounds(runtime->metals[index].position))) return true;
    }
    return false;
}

typedef struct RpgMetalObstacleContext {
    const RpgMagnetRuntime *runtime;
    int ignoredIndex;
} RpgMetalObstacleContext;

static bool DoesMetalCollideCallback(void *context, Rectangle bounds)
{
    const RpgMetalObstacleContext *metalContext = (const RpgMetalObstacleContext *)context;
    return metalContext != NULL && DoesMetalCollide(metalContext->runtime,
                                                    metalContext->ignoredIndex, bounds);
}

/* One-way floors are traversal surfaces, not opaque magnetic obstacles. */
static bool StopsMagnetField(int blockType)
{
    return blockType != 0 && !RpgBlockInventory_IsOneWayPlatform(blockType);
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
        if (StopsMagnetField(stage->blocks[row][column])) return false;
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
                if (!IsMagnetOuterEdgeDirection(stage, row, column, directionRow, directionColumn))
                    continue;
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
    RpgPhysicsBody body;
    if (metal->lockedSocketAttachmentIndex >= 0) return 0.0f;
    float start = vertical ? metal->position.y : metal->position.x;
    RpgMetalObstacleContext context = { .runtime = runtime, .ignoredIndex = metalIndex };
    body = (RpgPhysicsBody){
        .position = metal->position,
        .verticalSpeed = metal->verticalSpeed,
        .isGrounded = metal->isGrounded,
        .localBounds = { 0.0f, 0.0f, RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }
    };
    if (vertical)
        (void)RpgPhysics_MoveAxis(stage, &body.position, body.localBounds, amount, true,
                                  DoesMetalCollideCallback, &context);
    else
        (void)RpgPhysics_MoveHorizontalWithStep(stage, &body, amount,
                                                 DoesMetalCollideCallback, &context);
    metal->position = body.position;
    metal->verticalSpeed = body.verticalSpeed;
    metal->isGrounded = body.isGrounded;
    return (vertical ? metal->position.y : metal->position.x) - start;
}

static void UpdateMetal(RpgMagnetRuntime *runtime, const RpgStage *stage, int metalIndex,
                        const RpgWires *wires, float pixelsPerSecond, float deltaTime)
{
    RpgMagnetMetal *metal = &runtime->metals[metalIndex];
    metal->previousPosition = metal->position;
    if (metal->lockedSocketAttachmentIndex >= 0) {
        metal->verticalSpeed = 0.0f;
        metal->isGrounded = true;
        /* The only permitted overlap with the supporting tile is its socket
           recess.  Both this settle distance and the static tile's missing
           collision slice use RPG_BLOCK_SOCKET_RECESS_DEPTH. */
        float remainingDepth = (float)RPG_BLOCK_SOCKET_RECESS_DEPTH - metal->socketSettleOffset;
        float settleDistance = fminf(fmaxf(remainingDepth, 0.0f),
                                     RPG_BLOCK_SOCKET_SETTLE_SPEED * deltaTime);
        metal->position.y += settleDistance;
        metal->socketSettleOffset += settleDistance;
        return;
    }
    Vector2 magnetTarget = { 0 };
    if (FindMagnetTarget(runtime, stage, metalIndex, &magnetTarget)) {
        float dx = magnetTarget.x - metal->position.x;
        float dy = magnetTarget.y - metal->position.y;
        float distance = pixelsPerSecond * deltaTime;
        if (fabsf(dx) > 0.01f) MoveMetalAxis(runtime, stage, metalIndex,
                                              fmaxf(-distance, fminf(dx, distance)), false);
        else if (fabsf(dy) > 0.01f) MoveMetalAxis(runtime, stage, metalIndex,
                                                   fmaxf(-distance, fminf(dy, distance)), true);
    } else {
        /* 磁場に無い金属だけ重力で連続落下する。 */
        RpgMetalObstacleContext context = { .runtime = runtime, .ignoredIndex = metalIndex };
        RpgPhysicsBody body = {
            .position = metal->position,
            .verticalSpeed = metal->verticalSpeed,
            .isGrounded = metal->isGrounded,
            .localBounds = { 0.0f, 0.0f, RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }
        };
        /* Free movable blocks run through the exact same gravity and one-way
           floor path as the player.  Their only difference is that they have
           no direct input. */
        RpgPhysics_UpdateBody(stage, &body, 0.0f, 1200.0f, deltaTime,
                              DoesMetalCollideCallback, &context);
        metal->position = body.position;
        metal->verticalSpeed = body.verticalSpeed;
        metal->isGrounded = body.isGrounded;
    }
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

void RpgMagnets_Update(RpgMagnetRuntime *runtime, RpgStage *stage, const RpgWires *wires,
                       float pixelsPerSecond, float deltaTime,
                       const RpgPlayerPushState *pushState)
{
    if (runtime == NULL || stage == NULL || deltaTime <= 0.0f) return;
    (void)wires;
    /* Holding is a player-side relationship only.  It must not pause the
       block's own gravity, magnet attraction, or moving-solid physics. */
    (void)pushState;
    RpgMagnets_InitializeForStage(runtime, stage);
    if (pixelsPerSecond < 1.0f) pixelsPerSecond = 1.0f;
    for (int index = 0; index < runtime->metalCount; index++)
        if (runtime->metals[index].active)
            UpdateMetal(runtime, stage, index, NULL, pixelsPerSecond, deltaTime);
    RefreshMovingSolids(runtime);
}

RpgMovingSolidSet RpgMagnets_GetMovingSolids(const RpgMagnetRuntime *runtime)
{
    if (runtime == NULL || !runtime->isInitialized) return (RpgMovingSolidSet){ 0 };
    return (RpgMovingSolidSet){ .entries = runtime->movingSolids, .count = runtime->metalCount };
}

int RpgMagnets_FindBlockHit(const RpgMagnetRuntime *runtime, Rectangle bounds)
{
    if (runtime == NULL || !runtime->isInitialized) return -1;
    for (int index = 0; index < runtime->metalCount; index++)
        if (runtime->metals[index].active &&
            CheckCollisionRecs(bounds, GetMetalBounds(runtime->metals[index].position))) return index;
    return -1;
}

bool RpgMagnets_SetBlockZipperHeld(RpgMagnetRuntime *runtime, int index, bool isHeld)
{
    if (runtime == NULL || !runtime->isInitialized || index < 0 || index >= runtime->metalCount)
        return false;
    RpgMagnetMetal *block = &runtime->metals[index];
    block->active = !isHeld;
    block->verticalSpeed = 0.0f;
    block->isGrounded = false;
    block->lockedSocketAttachmentIndex = -1;
    block->requiresSocketDeparture = false;
    block->previousPosition = block->position;
    runtime->movingSolids[index] = block->active ? (RpgMovingSolid){
        .previousBounds = GetMetalBounds(block->position),
        .bounds = GetMetalBounds(block->position)
    } : (RpgMovingSolid){ 0 };
    return true;
}

void RpgMagnets_ReleaseBlocksLockedToAttachmentOwner(RpgMagnetRuntime *runtime,
                                                      const RpgAttachments *attachments,
                                                      RpgGridCell ownerBlockCell,
                                                      RpgPlayerPushState *pushState)
{
    if (runtime == NULL || attachments == NULL || !runtime->isInitialized) return;
    for (int index = 0; index < runtime->metalCount; index++) {
        RpgMagnetMetal *block = &runtime->metals[index];
        int socketIndex = block->lockedSocketAttachmentIndex;
        if (!block->active || socketIndex < 0 || socketIndex >= attachments->count) continue;
        const RpgAttachment *socket = &attachments->entries[socketIndex];
        if (socket->type != RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET ||
            !RpgAttachments_IsOwnedByBlock(socket, ownerBlockCell)) continue;
        block->lockedSocketAttachmentIndex = -1;
        block->socketSettleOffset = 0.0f;
        block->requiresSocketDeparture = false;
        block->verticalSpeed = 0.0f;
        block->isGrounded = false;
        block->previousPosition = block->position;
        if (pushState != NULL && pushState->heldBlockIndex == index)
            *pushState = RpgPlayerPushState_Default();
    }
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

bool RpgMagnets_TogglePlayerPush(RpgMagnetRuntime *runtime, RpgStage *stage,
                                 RpgPlayerPushState *pushState, RpgCharacter *player,
                                 float maximumDistance, RpgButtonEvent *event)
{
    if (runtime == NULL || stage == NULL || pushState == NULL || player == NULL) return false;
    (void)event;
    RpgMagnets_InitializeForStage(runtime, stage);
    if (RpgMagnets_IsPlayerPushHeld(runtime, pushState)) {
        /* Releasing without moving is a deliberate re-seat.  The next socket
           pass may lock this still-covered block immediately. */
        runtime->metals[pushState->heldBlockIndex].requiresSocketDeparture = false;
        *pushState = RpgPlayerPushState_Default();
        return true;
    }
    int closestIndex = -1;
    float closestDistance = maximumDistance;
    for (int index = 0; index < runtime->metalCount; index++) {
        const RpgMagnetMetal *block = &runtime->metals[index];
        if (!block->active || !RpgBlockInventory_IsPushBlock(block->blockType)) continue;
        float distance = Vector2Distance(player->position, GetPushBlockCenter(block));
        if (IsWithinPushBlockInteractionRange(player, block, maximumDistance) &&
            distance <= closestDistance) {
            closestDistance = distance;
            closestIndex = index;
        }
    }
    if (closestIndex < 0) return false;
    RpgMagnetMetal *block = &runtime->metals[closestIndex];
    {
        float blockCenterX = block->position.x + RPG_STAGE_TILE_SIZE * 0.5f;
        float side = blockCenterX < player->position.x ? -1.0f : 1.0f;
        /* Do not reject G merely because an immediate correction is blocked.
           The carry update resolves the one-tile relation through the same
           axis physics on the next world tick. */
        pushState->playerToBlockCenterOffsetX = side * RPG_STAGE_TILE_SIZE;
        pushState->carryDistanceAligned = false;
    }
    if (block->lockedSocketAttachmentIndex >= 0) {
        block->lockedSocketAttachmentIndex = -1;
        block->socketSettleOffset = 0.0f;
        /* Keep emitting while it still covers the aperture, but require a
           real departure before it may become fixed again. */
        block->requiresSocketDeparture = true;
    }
    pushState->heldBlockIndex = closestIndex;
    return true;
}

bool RpgMagnets_MoveHeldPushBlock(RpgMagnetRuntime *runtime, const RpgStage *stage,
                                  RpgPlayerPushState *pushState, RpgCharacter *player,
                                  float playerMovementX)
{
    const float alignmentTolerance = 1.0f;
    if (!RpgMagnets_IsPlayerPushHeld(runtime, pushState) || player == NULL) return false;
    int index = pushState->heldBlockIndex;
    RpgMagnetMetal *block = &runtime->metals[index];
    float centerOffset = block->position.x + RPG_STAGE_TILE_SIZE * 0.5f - player->position.x;
    /* Align once through the shared collision solver.  A later collision can
       leave the pair a fraction (or more) away from the ideal one-tile
       offset; that must not end carrying while G could still reach it. */
    if (!pushState->carryDistanceAligned &&
        fabsf(centerOffset - pushState->playerToBlockCenterOffsetX) > alignmentTolerance) {
        Rectangle playerBounds = RpgCharacter_GetCollisionBounds(player);
        Rectangle playerLocalBounds = { playerBounds.x - player->position.x,
                                        playerBounds.y - player->position.y,
                                        playerBounds.width, playerBounds.height };
        RpgMetalObstacleContext collisionContext = { .runtime = runtime, .ignoredIndex = index };
        Vector2 originalPosition = player->position;
        float targetPlayerX = block->position.x + RPG_STAGE_TILE_SIZE * 0.5f -
                              pushState->playerToBlockCenterOffsetX;
        if (RpgPhysics_MoveAxis(stage, &player->position, playerLocalBounds,
                                targetPlayerX - player->position.x, false,
                                DoesMetalCollideCallback, &collisionContext)) {
            float targetBlockX = player->position.x + pushState->playerToBlockCenterOffsetX -
                                 RPG_STAGE_TILE_SIZE * 0.5f;
            player->position = originalPosition;
            (void)MoveMetalAxis(runtime, stage, index, targetBlockX - block->position.x, false);
        }
        centerOffset = block->position.x + RPG_STAGE_TILE_SIZE * 0.5f - player->position.x;
        if (fabsf(centerOffset - pushState->playerToBlockCenterOffsetX) > alignmentTolerance)
            pushState->playerToBlockCenterOffsetX = centerOffset;
    }
    pushState->carryDistanceAligned = true;
    runtime->metals[index].previousPosition = runtime->metals[index].position;
    float blockMovementX = MoveMetalAxis(runtime, stage, index, playerMovementX, false);
    float playerCorrectionX = blockMovementX - playerMovementX;
    if (fabsf(playerCorrectionX) > 0.001f) {
        Rectangle playerBounds = RpgCharacter_GetCollisionBounds(player);
        Rectangle playerLocalBounds = { playerBounds.x - player->position.x,
                                        playerBounds.y - player->position.y,
                                        playerBounds.width, playerBounds.height };
        RpgMetalObstacleContext collisionContext = { .runtime = runtime, .ignoredIndex = index };
        Vector2 originalPosition = player->position;
        if (RpgPhysics_MoveAxis(stage, &player->position, playerLocalBounds, playerCorrectionX,
                                false, DoesMetalCollideCallback, &collisionContext)) {
            player->position = originalPosition;
            *pushState = RpgPlayerPushState_Default();
            return false;
        }
    }
    if (!IsWithinPushBlockInteractionRange(player, block,
                                           (float)RPG_PUSH_BLOCK_INTERACTION_RANGE)) {
        *pushState = RpgPlayerPushState_Default();
        return false;
    }
    return true;
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
        if (metal->lockedSocketAttachmentIndex >= 0) {
            DrawRectangleRec(bounds, Fade((Color){ 36, 159, 255, 255 }, 0.22f));
            DrawRectangleLinesEx((Rectangle){ bounds.x + 1.0f, bounds.y + 1.0f,
                                               bounds.width - 2.0f, bounds.height - 2.0f },
                                 2.0f, Fade((Color){ 120, 221, 255, 255 }, 0.96f));
        }
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
                if (!IsMagnetOuterEdgeDirection(stage, row, column, directionRow, directionColumn))
                    continue;
                for (int distance = 1;; distance++) {
                    int targetRow = row + directionRow * distance;
                    int targetColumn = column + directionColumn * distance;
                    if (!IsCellInsideStage(targetRow, targetColumn)) break;
                    int blockType = stage->blocks[targetRow][targetColumn];
                    Vector2 target = { targetColumn * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f,
                                       targetRow * RPG_STAGE_TILE_SIZE + RPG_STAGE_TILE_SIZE * 0.5f };
                    DrawLineEx(center, target, 1.5f, Fade(SKYBLUE, 0.28f));
                    if (StopsMagnetField(blockType)) break;
                }
            }
        }
    }
}
