// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_button_event.h, rpg_character.h, rpg_data_shot.h, rpg_layout.h, rpg_magnet.h, rpg_receiver.h, rpg_signal_block.h, rpg_stage.h, rpg_wire.h
// 役割: RPG本編とエディター内プレイで共通に使う、移動・信号・データ弾の1フレーム更新を定義する。
#ifndef RPG_RUNTIME_UPDATE_H
#define RPG_RUNTIME_UPDATE_H

#include <stdbool.h>

#include "rpg_attachment.h"
#include "rpg_button_event.h"
#include "rpg_character.h"
#include "rpg_data_shot.h"
#include "rpg_layout.h"
#include "rpg_magnet.h"
#include "rpg_receiver.h"
#include "rpg_signal_block.h"
#include "rpg_stage.h"
#include "rpg_wire.h"

typedef struct RpgRuntimeUpdateContext {
    RpgCharacter *player;
    const RpgCharacter *npc;
    RpgStage *stage;
    RpgAttachments *attachments;
    RpgSignalBlocks *signalBlocks;
    RpgDataShots *dataShots;
    RpgButtonEvent *buttonEvent;
    RpgReceivers *receivers;
    RpgWires *wires;
    RpgMagnetRuntime *magnetRuntime;
    RpgPlayerPushState *playerPushState;
    const RpgLayout *layout;
    bool *wasButtonPressed;
    int currentMapIndex;
    bool acceptsPlayerInput;
    bool updatesWorldSystems;
} RpgRuntimeUpdateContext;

void RpgRuntime_UpdateWorld(RpgRuntimeUpdateContext *context, float deltaTime);

#endif
