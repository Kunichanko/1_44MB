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

    RpgCharacter *player = context->player;
    Vector2 previousPosition = player->position;
    int standingBlockType = RpgStage_GetBlockTypeAtPosition(context->stage, player->position);
    float savedMoveSpeed = player->moveSpeed;
    if (standingBlockType == RPG_BLOCK_EFFECT_SLOW) player->moveSpeed *= 0.55f;
    if (context->acceptsPlayerInput) {
        float maximumX = RpgStage_GetMapCount(context->stage) * RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE - 32.0f;
        RpgCharacter_UpdatePlayerWithStage(player, deltaTime, context->stage, 32.0f, maximumX);
    }
    player->moveSpeed = savedMoveSpeed;
    if (RpgBlockInventory_IsBounceEffect(standingBlockType) && player->isGrounded) {
        player->verticalSpeed = -620.0f;
        player->isGrounded = false;
    }
    if (CheckCollisionRecs(RpgCharacter_GetFootBounds(player), RpgCharacter_GetFootBounds(context->npc)))
        player->position = previousPosition;

    if (!context->updatesWorldSystems) return;
    bool isButtonPressed = player->isGrounded &&
                           RpgAttachments_IsButtonPressed(context->attachments, player->position);
    if (isButtonPressed && !*context->wasButtonPressed)
        RpgButtonEvent_Publish(context->buttonEvent);
    *context->wasButtonPressed = isButtonPressed;
    RpgDataShots_ConsumeButtonEvent(context->dataShots, context->attachments, context->buttonEvent);
    RpgSignalBlocks_Update(context->signalBlocks, context->stage, context->buttonEvent, deltaTime);
    RpgDataShots_Update(context->dataShots, context->attachments, context->stage, context->receivers,
                         context->wires, context->layout->electricCellDelay, deltaTime, false);
}
