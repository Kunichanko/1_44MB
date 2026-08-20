// 依存する自プロジェクト内ファイル: rpg_editor_play.h
// 役割: エディター内プレイの開始・停止に必要な編集状態の複製と復元を実装する。
#include "rpg_editor_play.h"

#include <stddef.h>

void RpgEditorPlay_Begin(RpgEditorPlaySnapshot *snapshot, int mapIndex,
                         const RpgCharacter *player, const RpgCharacter *npc,
                         const RpgStage *stage, const RpgItems *items,
                         const RpgMapEvents *mapEvents, const RpgWires *wires,
                         const RpgReceivers *receivers, const RpgAttachments *attachments,
                         const RpgSignalBlocks *signalBlocks, const RpgZipper *zipper)
{
    if (snapshot == NULL || player == NULL || npc == NULL || stage == NULL || items == NULL ||
        mapEvents == NULL || wires == NULL || receivers == NULL || attachments == NULL ||
        signalBlocks == NULL || zipper == NULL) return;
    snapshot->active = true;
    snapshot->mapIndex = mapIndex;
    snapshot->player = *player;
    snapshot->npc = *npc;
    snapshot->stage = *stage;
    snapshot->items = *items;
    snapshot->mapEvents = *mapEvents;
    snapshot->wires = *wires;
    snapshot->receivers = *receivers;
    snapshot->attachments = *attachments;
    snapshot->signalBlocks = *signalBlocks;
    snapshot->zipper = *zipper;
}

bool RpgEditorPlay_Stop(RpgEditorPlaySnapshot *snapshot, int *mapIndex,
                        RpgCharacter *player, RpgCharacter *npc, RpgStage *stage,
                        RpgItems *items, RpgMapEvents *mapEvents, RpgWires *wires,
                        RpgReceivers *receivers, RpgAttachments *attachments,
                        RpgSignalBlocks *signalBlocks, RpgZipper *zipper)
{
    if (snapshot == NULL || !snapshot->active || mapIndex == NULL || player == NULL || npc == NULL ||
        stage == NULL || items == NULL || mapEvents == NULL || wires == NULL || receivers == NULL ||
        attachments == NULL || signalBlocks == NULL || zipper == NULL) return false;
    *mapIndex = snapshot->mapIndex;
    *player = snapshot->player;
    *npc = snapshot->npc;
    *stage = snapshot->stage;
    *items = snapshot->items;
    *mapEvents = snapshot->mapEvents;
    *wires = snapshot->wires;
    *receivers = snapshot->receivers;
    *attachments = snapshot->attachments;
    *signalBlocks = snapshot->signalBlocks;
    *zipper = snapshot->zipper;
    snapshot->active = false;
    return true;
}
