#include "rpg_zipper_transfer.h"

#include "rpg_block_inventory.h"
#include "rpg_object_folder.h"

/* A transfer has exactly two public policies: BLOCK creates/removes a missing
 * terrain cell, MOVABLE only moves its own runtime folder.  movableKind merely
 * identifies the concrete folder and runtime state to update. */
static void SetBlockMissingState(RpgStage *stage, const RpgZipperHeldObject *target, bool missing)
{
    RpgGridCell cell = target == NULL ? (RpgGridCell){ -1, -1 } : target->blockCell;
    const RpgEffectShape *shape;
    if (stage == NULL || target == NULL || cell.row < 0 || cell.row >= RPG_STAGE_ROWS ||
        cell.column < 0 || cell.column >= RPG_STAGE_WORLD_COLUMNS) return;
    shape = RpgBlockInventory_GetEffectShape(target->blockType);
    if (shape == NULL || shape->rootType != target->blockType) {
        if (missing) {
            stage->missingBlockTypes[cell.row][cell.column] = target->blockType;
            stage->blocks[cell.row][cell.column] = RPG_BLOCK_BUILD_MISSING;
        } else if (stage->blocks[cell.row][cell.column] == RPG_BLOCK_BUILD_MISSING) {
            stage->blocks[cell.row][cell.column] = target->blockType;
            stage->missingBlockTypes[cell.row][cell.column] = 0;
        }
        return;
    }
    for (int index = 0; index < shape->cellCount; index++) {
        const RpgEffectShapeCell *part = &shape->cells[index];
        int row = cell.row + part->offsetY;
        int column = cell.column + part->offsetX;
        if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) continue;
        if (missing) {
            stage->missingBlockTypes[row][column] = part->blockType;
            stage->blocks[row][column] = RPG_BLOCK_BUILD_MISSING;
        } else if (stage->blocks[row][column] == RPG_BLOCK_BUILD_MISSING) {
            stage->blocks[row][column] = part->blockType;
            stage->missingBlockTypes[row][column] = 0;
        }
    }
}

static bool EatMovable(RpgZipperTransferContext *context, const RpgZipperHeldObject *target)
{
    switch (target->movableKind) {
    case RPG_ZIPPER_MOVABLE_DATA_SHOT:
        if (context->dataShots == NULL || target->dataShotIndex < 0 ||
            target->dataShotIndex >= RPG_DATA_SHOT_MAX_COUNT) return false;
        if (!RpgObjectFolder_MoveDataShotToZipper(&context->dataShots->entries[target->dataShotIndex])) return false;
        context->dataShots->entries[target->dataShotIndex].isZipperHeld = true;
        return true;
    case RPG_ZIPPER_MOVABLE_DYNAMIC_BLOCK:
        if (context->magnetRuntime == NULL || target->dynamicBlockIndex < 0 ||
            target->dynamicBlockIndex >= context->magnetRuntime->metalCount) return false;
        {
            RpgMagnetMetal *block = &context->magnetRuntime->metals[target->dynamicBlockIndex];
            if (!block->active || !RpgObjectFolder_MoveDynamicBlockToZipper(block->objectCell,
                    block->blockType, block->position) ||
                !RpgMagnets_SetBlockZipperHeld(context->magnetRuntime, target->dynamicBlockIndex, true)) return false;
            if (context->playerPushState != NULL &&
                context->playerPushState->heldBlockIndex == target->dynamicBlockIndex)
                *context->playerPushState = RpgPlayerPushState_Default();
            return true;
        }
    case RPG_ZIPPER_MOVABLE_REFERENCE_FILE:
        if (context->referenceObjects == NULL || target->referenceObjectIndex < 0 ||
            target->referenceObjectIndex >= context->referenceObjects->count ||
            target->referenceObject.objectKind != RPG_REFERENCE_OBJECT_FILE) return false;
        if (!RpgObjectFolder_MoveReferenceFileToZipper(&target->referenceObject)) return false;
        return RpgReferenceObjects_RemoveTarget(context->stage, context->referenceObjects,
                                                (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_DROP,
                                                                      .row = -1, .column = -1,
                                                                      .dropIndex = target->referenceObjectIndex });
    default:
        return false;
    }
}

static bool BeginSpitMovable(RpgZipperTransferContext *context, const RpgZipperHeldObject *target)
{
    switch (target->movableKind) {
    case RPG_ZIPPER_MOVABLE_DATA_SHOT:
        return context->dataShots != NULL && target->dataShotIndex >= 0 &&
               target->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT &&
               RpgObjectFolder_BeginReturnDataShotFromZipper(&context->dataShots->entries[target->dataShotIndex]);
    case RPG_ZIPPER_MOVABLE_DYNAMIC_BLOCK:
        return context->magnetRuntime != NULL && target->dynamicBlockIndex >= 0 &&
               target->dynamicBlockIndex < context->magnetRuntime->metalCount &&
               RpgObjectFolder_BeginReturnDynamicBlockFromZipper(
                   context->magnetRuntime->metals[target->dynamicBlockIndex].objectCell,
                   context->magnetRuntime->metals[target->dynamicBlockIndex].blockType,
                   context->magnetRuntime->metals[target->dynamicBlockIndex].position);
    case RPG_ZIPPER_MOVABLE_REFERENCE_FILE:
        return RpgObjectFolder_BeginReturnReferenceFileFromZipper(&target->referenceObject);
    default:
        return false;
    }
}

static bool CompleteSpitMovable(RpgZipperTransferContext *context, const RpgZipperHeldObject *target)
{
    switch (target->movableKind) {
    case RPG_ZIPPER_MOVABLE_DATA_SHOT:
        if (context->dataShots == NULL || target->dataShotIndex < 0 ||
            target->dataShotIndex >= RPG_DATA_SHOT_MAX_COUNT) return false;
        if (!RpgObjectFolder_ReturnDataShotFromZipper(&context->dataShots->entries[target->dataShotIndex])) return false;
        (void)RpgObjectFolder_RestoreDataShotFromMetadata(&context->dataShots->entries[target->dataShotIndex]);
        context->dataShots->entries[target->dataShotIndex].isZipperHeld = false;
        return true;
    case RPG_ZIPPER_MOVABLE_DYNAMIC_BLOCK:
        if (context->magnetRuntime == NULL || target->dynamicBlockIndex < 0 ||
            target->dynamicBlockIndex >= context->magnetRuntime->metalCount) return false;
        {
            RpgMagnetMetal *block = &context->magnetRuntime->metals[target->dynamicBlockIndex];
            if (!RpgObjectFolder_ReturnDynamicBlockFromZipper(block->objectCell, block->blockType,
                                                               block->position)) return false;
            return RpgMagnets_SetBlockZipperHeld(context->magnetRuntime, target->dynamicBlockIndex, false);
        }
    case RPG_ZIPPER_MOVABLE_REFERENCE_FILE:
        if (context->referenceObjects == NULL ||
            !RpgObjectFolder_ReturnReferenceFileFromZipper(&target->referenceObject) ||
            !RpgReferenceObjects_Add(context->referenceObjects, target->referenceObject.objectKind,
                                     target->referenceObject.position, target->referenceObject.path,
                                     target->referenceObject.id)) return false;
        context->referenceObjects->entries[context->referenceObjects->count - 1] = target->referenceObject;
        return true;
    default:
        return false;
    }
}

bool RpgZipperTransfer_Eat(RpgZipperTransferContext *context, const RpgZipperHeldObject *target)
{
    if (context == NULL || target == NULL) return false;
    if (target->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK) {
        if (!RpgObjectFolder_MoveBlockToZipper(&(RpgObjectFolder){ .cell = target->blockCell }, target->blockType))
            return false;
        SetBlockMissingState(context->stage, target, true);
        return true;
    }
    return target->kind == RPG_ZIPPER_HELD_OBJECT_MOVABLE && EatMovable(context, target);
}

bool RpgZipperTransfer_BeginSpit(RpgZipperTransferContext *context, const RpgZipperHeldObject *target)
{
    if (context == NULL || target == NULL) return false;
    if (target->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK)
        return RpgObjectFolder_BeginReturnBlockFromZipper(&(RpgObjectFolder){ .cell = target->blockCell },
                                                           target->blockType);
    return target->kind == RPG_ZIPPER_HELD_OBJECT_MOVABLE && BeginSpitMovable(context, target);
}

bool RpgZipperTransfer_CompleteSpit(RpgZipperTransferContext *context, const RpgZipperHeldObject *target)
{
    if (context == NULL || target == NULL) return false;
    if (target->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK) {
        bool returned = RpgObjectFolder_ReturnBlockFromZipper(&(RpgObjectFolder){ .cell = target->blockCell },
                                                               target->blockType);
        if (returned) SetBlockMissingState(context->stage, target, false);
        return returned;
    }
    return target->kind == RPG_ZIPPER_HELD_OBJECT_MOVABLE && CompleteSpitMovable(context, target);
}

Vector2 RpgZipperTransfer_GetReturnDestination(const RpgZipperTransferContext *context,
                                                const RpgZipperHeldObject *target, Vector2 fallback)
{
    if (context == NULL || target == NULL) return fallback;
    if (target->kind == RPG_ZIPPER_HELD_OBJECT_BLOCK && context->stage != NULL)
        return RpgStage_GetWorldPositionForCell(context->stage, target->blockCell.row, target->blockCell.column);
    if (target->kind != RPG_ZIPPER_HELD_OBJECT_MOVABLE) return fallback;
    switch (target->movableKind) {
    case RPG_ZIPPER_MOVABLE_DATA_SHOT:
        if (context->dataShots != NULL && target->dataShotIndex >= 0 &&
            target->dataShotIndex < RPG_DATA_SHOT_MAX_COUNT)
            return context->dataShots->entries[target->dataShotIndex].position;
        break;
    case RPG_ZIPPER_MOVABLE_DYNAMIC_BLOCK:
        if (context->magnetRuntime != NULL && target->dynamicBlockIndex >= 0 &&
            target->dynamicBlockIndex < context->magnetRuntime->metalCount) {
            const RpgMagnetMetal *block = &context->magnetRuntime->metals[target->dynamicBlockIndex];
            return (Vector2){ block->position.x + RPG_STAGE_TILE_SIZE * 0.5f,
                              block->position.y + RPG_STAGE_TILE_SIZE * 0.5f };
        }
        break;
    case RPG_ZIPPER_MOVABLE_REFERENCE_FILE:
        return target->referenceObject.position;
    default:
        break;
    }
    return fallback;
}
