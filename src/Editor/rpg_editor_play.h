// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_character.h, rpg_item.h, rpg_map_event.h, rpg_receiver.h, rpg_signal_block.h, rpg_stage.h, rpg_wire.h, rpg_zipper.h
// 役割: エディター内プレイ開始前の編集状態を保持し、停止時に安全に復元する。
#ifndef RPG_EDITOR_PLAY_H
#define RPG_EDITOR_PLAY_H

#include "rpg_attachment.h"
#include "rpg_character.h"
#include "rpg_item.h"
#include "rpg_map_event.h"
#include "rpg_receiver.h"
#include "rpg_signal_block.h"
#include "rpg_stage.h"
#include "rpg_wire.h"
#include "rpg_zipper.h"

typedef struct RpgEditorPlaySnapshot {
    bool active;
    int mapIndex;
    RpgCharacter player;
    RpgCharacter npc;
    RpgStage stage;
    RpgItems items;
    RpgMapEvents mapEvents;
    RpgWires wires;
    RpgReceivers receivers;
    RpgAttachments attachments;
    RpgSignalBlocks signalBlocks;
    RpgZipper zipper;
} RpgEditorPlaySnapshot;

void RpgEditorPlay_Begin(RpgEditorPlaySnapshot *snapshot, int mapIndex,
                         const RpgCharacter *player, const RpgCharacter *npc,
                         const RpgStage *stage, const RpgItems *items,
                         const RpgMapEvents *mapEvents, const RpgWires *wires,
                         const RpgReceivers *receivers, const RpgAttachments *attachments,
                         const RpgSignalBlocks *signalBlocks, const RpgZipper *zipper);
bool RpgEditorPlay_Stop(RpgEditorPlaySnapshot *snapshot, int *mapIndex,
                        RpgCharacter *player, RpgCharacter *npc, RpgStage *stage,
                        RpgItems *items, RpgMapEvents *mapEvents, RpgWires *wires,
                        RpgReceivers *receivers, RpgAttachments *attachments,
                        RpgSignalBlocks *signalBlocks, RpgZipper *zipper);

#endif
