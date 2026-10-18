// 依存する自プロジェクト内ファイル: rpg_runtime_update.h, rpg_block_inventory.h
// 役割: 本編とエディター内プレイで共通の地面判定付き移動、信号、データ弾更新を実装する。
#include "rpg_runtime_update.h"

#include <stddef.h>

#include "rpg_block_inventory.h"

void RpgRuntime_UpdateWorld(RpgRuntimeUpdateContext *context, float deltaTime)
{
    if (context == NULL || context->player == NULL || context->npc == NULL || context->stage == NULL ||
        context->attachments == NULL || context->signalBlocks == NULL || context->dataShots == NULL ||
        context->buttonEvent == NULL || context->socketCommunicationEvent == NULL ||
        context->receivers == NULL || context->wires == NULL ||
        context->layout == NULL || context->wasButtonPressed == NULL) return;

    /* 本編は入力用の前段と世界更新の後段からこの関数を呼ぶ。前段では何も更新せず、
       同じフレームのプレイヤー物理を二重に進めない。 */
    if (!context->updatesWorldSystems) return;

    RpgCharacter *player = context->player;
    RpgMagnets_InitializeForStage(context->magnetRuntime, context->stage);
    RpgMagnets_BeginFrame(context->magnetRuntime);
    /* Socket occupancy is tested before consumer systems read this frame's
       button event, so an already seated block can fire the normal area signal
       without a special signal pipeline. */
    RpgMagnets_LockInBlockSockets(context->magnetRuntime, context->stage, context->attachments,
                                  context->socketCommunicationEvent, context->playerPushState);
    RpgMagnets_UpdateBlockSocketSignals(context->magnetRuntime, context->stage,
                                        context->attachments, context->socketCommunicationEvent);
    RpgMovingSolidSet dataShotSolids = RpgMagnets_GetMovingSolids(context->magnetRuntime);
    RpgMovingSolidSet movingSolids = dataShotSolids;
    bool isHoldingPushBlock = RpgMagnets_IsPlayerPushHeld(context->magnetRuntime,
                                                           context->playerPushState);
    Vector2 previousPosition = player->position;
    int standingBlockType = RpgStage_GetBlockTypeAtPosition(context->stage, player->position);
    float savedMoveSpeed = player->moveSpeed;
    if (standingBlockType == RPG_BLOCK_EFFECT_SLOW) player->moveSpeed *= 0.55f;
    RpgStage_SetSpatialReferenceMap(context->stage, context->currentMapIndex);
    if (context->acceptsPlayerInput) {
        previousPosition = player->position;
        /* The stored map slots start at x=0, while the connected stage may extend
           into negative grid coordinates.  Permit one tile beyond both storage ends
           so the shared area-transition code can remap the player to its neighbour
           before a global storage clamp hides left/topology areas. */
        float minimumX = -RPG_STAGE_TILE_SIZE;
        float maximumX = RPG_STAGE_WORLD_WIDTH + RPG_STAGE_TILE_SIZE;
        float playerXBeforeCarryMotion = player->position.x;
        RpgCharacter_UpdatePlayerWithStageAndMovingSolidsControlled(player, deltaTime, context->stage,
                                                                     &movingSolids, minimumX, maximumX,
                                                                     !isHoldingPushBlock || player->isGrounded,
                                                                     !isHoldingPushBlock);
        if (isHoldingPushBlock) {
            (void)RpgMagnets_MoveHeldPushBlock(context->magnetRuntime, context->stage,
                                                context->playerPushState, player,
                                                player->position.x - playerXBeforeCarryMotion);
            /* The carried block can arrive over a socket during this input
               step.  Test again after its shared movement path, so locking
               happens immediately rather than requiring another frame or G. */
            RpgMagnets_LockInBlockSockets(context->magnetRuntime, context->stage,
                                          context->attachments, context->socketCommunicationEvent,
                                          context->playerPushState);
            RpgMagnets_UpdateBlockSocketSignals(context->magnetRuntime, context->stage,
                                                context->attachments, context->socketCommunicationEvent);
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
    if (isButtonPressed != *context->wasButtonPressed)
        RpgButtonEvent_PublishState(context->buttonEvent, context->currentMapIndex,
                                    RPG_BUTTON_EVENT_SOURCE_PLAYER_BUTTON,
                                    isButtonPressed);
    *context->wasButtonPressed = isButtonPressed;
    RpgMagnets_ConsumeCommunication(context->magnetRuntime, context->stage,
                                    context->socketCommunicationEvent);
    RpgDataShots_ConsumeButtonEvent(context->dataShots, context->attachments, context->buttonEvent);
    RpgSignalBlocks_Update(context->signalBlocks, context->stage, context->buttonEvent, deltaTime);
    RpgDataShots_Update(context->dataShots, context->attachments, context->stage, context->receivers,
                         context->wires, context->layout->electricCellDelay, &dataShotSolids, deltaTime, false);
    RpgMagnets_Update(context->magnetRuntime, context->stage, context->wires,
                      context->layout->magnetMetalSpeed, deltaTime,
                      context->playerPushState);
    /* Magnets, gravity and conveyors can also complete a socket alignment.
       Recheck after their movement so a block never loses its socket property
       merely because it was moving rather than being carried by the player. */
    RpgMagnets_LockInBlockSockets(context->magnetRuntime, context->stage, context->attachments,
                                  context->socketCommunicationEvent, context->playerPushState);
    RpgMagnets_UpdateBlockSocketSignals(context->magnetRuntime, context->stage,
                                        context->attachments, context->socketCommunicationEvent);
    /* A carried block remains a moving solid throughout the whole frame.  The
       pair synchronizer above maintains the one-tile gap; this shared resolver
       still handles contacts with every other moving object. */
    isHoldingPushBlock = RpgMagnets_IsPlayerPushHeld(context->magnetRuntime,
                                                      context->playerPushState);
    movingSolids = RpgMagnets_GetMovingSolids(context->magnetRuntime);
    RpgCharacter_ResolveMovingSolidContacts(player, context->stage, &movingSolids);
    if (isHoldingPushBlock)
        (void)RpgMagnets_MoveHeldPushBlock(context->magnetRuntime, context->stage,
                                            context->playerPushState, player, 0.0f);
}
