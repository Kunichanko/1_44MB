// 依存する自プロジェクト内ファイル: rpg_runtime_update.h, rpg_block_inventory.h
// 役割: 本編とエディター内プレイで共通の地面判定付き移動、信号、データ弾更新を実装する。
#include "rpg_runtime_update.h"

#include <stddef.h>

#include "rpg_block_inventory.h"

void RpgRuntime_UpdateWorld(RpgRuntimeUpdateContext *context, float deltaTime)
{
    if (context == NULL || context->player == NULL || context->npc == NULL || context->stage == NULL ||
        context->attachments == NULL || context->signalBlocks == NULL || context->dataShots == NULL ||
        context->buttonEvent == NULL || context->receivers == NULL || context->wires == NULL ||
        context->layout == NULL || context->wasButtonPressed == NULL) return;

    /* 本編は入力用の前段と世界更新の後段からこの関数を呼ぶ。前段では何も更新せず、
       同じフレームのプレイヤー物理を二重に進めない。 */
    if (!context->updatesWorldSystems) return;

    RpgCharacter *player = context->player;
    RpgMagnets_InitializeForStage(context->magnetRuntime, context->stage);
    RpgMagnets_BeginFrame(context->magnetRuntime);
    RpgMovingSolidSet dataShotSolids = RpgMagnets_GetMovingSolids(context->magnetRuntime);
    RpgMovingSolidSet movingSolids = dataShotSolids;
    RpgMovingSolid playerSolidStorage[RPG_MAGNET_MAX_METALS];
    bool isHoldingPushBlock = RpgMagnets_IsPlayerPushHeld(context->magnetRuntime,
                                                           context->playerPushState);
    if (isHoldingPushBlock)
        movingSolids = RpgMagnets_GetMovingSolidsExcept(context->magnetRuntime,
                                                         context->playerPushState->heldBlockIndex,
                                                         playerSolidStorage, RPG_MAGNET_MAX_METALS);
    Vector2 previousPosition = player->position;
    int standingBlockType = RpgStage_GetBlockTypeAtPosition(context->stage, player->position);
    float savedMoveSpeed = player->moveSpeed;
    if (standingBlockType == RPG_BLOCK_EFFECT_SLOW) player->moveSpeed *= 0.55f;
    RpgStage_SetSpatialReferenceMap(context->stage, context->currentMapIndex);
    if (context->acceptsPlayerInput) {
        /* The stored map slots start at x=0, while the connected stage may extend
           into negative grid coordinates.  Permit one tile beyond both storage ends
           so the shared area-transition code can remap the player to its neighbour
           before a global storage clamp hides left/topology areas. */
        float minimumX = -RPG_STAGE_TILE_SIZE;
        float maximumX = RPG_STAGE_WORLD_WIDTH + RPG_STAGE_TILE_SIZE;
        RpgCharacter_UpdatePlayerWithStageAndMovingSolidsControlled(player, deltaTime, context->stage,
                                                                     &movingSolids, minimumX, maximumX,
                                                                     !isHoldingPushBlock || player->isGrounded,
                                                                     !isHoldingPushBlock);
        if (isHoldingPushBlock) {
            float blockX = RpgMagnets_GetHeldPushBlockX(context->magnetRuntime, context->playerPushState);
            float horizontalMovement = player->position.x + context->playerPushState->playerToBlockOffsetX - blockX;
            float appliedMovement = RpgMagnets_MoveHeldPushBlock(context->magnetRuntime, context->stage,
                                                                  context->playerPushState, horizontalMovement);
            player->position.x = blockX + appliedMovement - context->playerPushState->playerToBlockOffsetX;
        }
    } else player->isMoving = false;
    RpgStage_SetSpatialReferenceMap(context->stage, -1);
    player->moveSpeed = savedMoveSpeed;
    if (RpgBlockInventory_IsBounceEffect(standingBlockType) && player->isGrounded) {
        player->verticalSpeed = -620.0f;
        player->isGrounded = false;
    }
    if (CheckCollisionRecs(RpgCharacter_GetFootBounds(player), RpgCharacter_GetFootBounds(context->npc)))
        player->position = previousPosition;

    if (!context->updatesWorldSystems) return;
    bool isButtonPressed = player->isGrounded &&
                           RpgAttachments_IsButtonPressedWorld(context->attachments, context->stage,
                                                               player->position);
    if (isButtonPressed && !*context->wasButtonPressed)
        RpgButtonEvent_Publish(context->buttonEvent, context->currentMapIndex);
    *context->wasButtonPressed = isButtonPressed;
    RpgDataShots_ConsumeButtonEvent(context->dataShots, context->attachments, context->buttonEvent);
    RpgSignalBlocks_Update(context->signalBlocks, context->stage, context->buttonEvent, deltaTime);
    RpgDataShots_Update(context->dataShots, context->attachments, context->stage, context->receivers,
                         context->wires, context->layout->electricCellDelay, &dataShotSolids, deltaTime, false);
    RpgMagnets_Update(context->magnetRuntime, context->stage, context->layout->magnetMetalSpeed, deltaTime,
                      context->playerPushState);
    /* A held push block is position-locked to the player below.  It must not
       also run through the generic moving-solid resolver, otherwise reversing
       direction makes that resolver push the holder away from their own block. */
    isHoldingPushBlock = RpgMagnets_IsPlayerPushHeld(context->magnetRuntime,
                                                      context->playerPushState);
    movingSolids = isHoldingPushBlock ?
        RpgMagnets_GetMovingSolidsExcept(context->magnetRuntime,
                                         context->playerPushState->heldBlockIndex,
                                         playerSolidStorage, RPG_MAGNET_MAX_METALS) :
        RpgMagnets_GetMovingSolids(context->magnetRuntime);
    RpgCharacter_ResolveMovingSolidContacts(player, context->stage, &movingSolids);
}
