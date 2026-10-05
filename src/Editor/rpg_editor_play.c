// 依存する自プロジェクト内ファイル: rpg_editor_play.h
// 役割: エディター内プレイの開始・停止に必要な編集状態の複製と復元を実装する。
#include "rpg_editor_play.h"
#include "rpg_stage_authority.h"

#include <stddef.h>

void RpgEditorPlay_Begin(RpgEditorPlayRuntime *runtime, int mapIndex, const RpgLayout *layout,
                         const RpgCharacter *player, const RpgCharacter *npc,
                         const RpgInspect *npcInspect,
                         const RpgStage *stage, const RpgDialogue *dialogue,
                         const RpgStage3Event *stage3Event, const RpgAreaEntryEvents *areaEntryEvents,
                         const RpgItems *items,
                         const RpgMapEvents *mapEvents, const RpgWires *wires,
                         const RpgReceivers *receivers, const RpgAttachments *attachments,
                         const RpgSignalBlocks *signalBlocks, const RpgZipper *zipper)
{
    if (runtime == NULL || layout == NULL || player == NULL || npc == NULL || npcInspect == NULL || stage == NULL || dialogue == NULL ||
        stage3Event == NULL || areaEntryEvents == NULL || items == NULL ||
        mapEvents == NULL || wires == NULL || receivers == NULL || attachments == NULL ||
        signalBlocks == NULL || zipper == NULL) return;
    runtime->active = true;
    runtime->mapIndex = mapIndex;
    runtime->layout = *layout;
    runtime->player = *player;
    runtime->npc = *npc;
    runtime->runtimeNpcInspect = *npcInspect;
    runtime->stage = *stage;
    runtime->dialogue = *dialogue;
    runtime->stage3Event = *stage3Event;
    runtime->areaEntryEvents = *areaEntryEvents;
    runtime->items = *items;
    runtime->mapEvents = *mapEvents;
    runtime->wires = *wires;
    if (!RpgAttachments_Clone(&runtime->attachments, attachments)) return;
    RpgReceivers_Bind(&runtime->receivers, &runtime->attachments);
    runtime->signalBlocks = *signalBlocks;
    runtime->zipper = *zipper;
    /* From this point until End, only Stage/editor runtime artifacts and the
       runtime projection are writable.  Static Settings cannot be saved by
       a Play-side storage call. */
    RpgStageAuthority_Enter(RPG_STAGE_AUTHORITY_EDITOR_RUNTIME);
}

void RpgEditorPlay_End(RpgEditorPlayRuntime *runtime)
{
    if (runtime != NULL) {
        runtime->active = false;
        RpgAttachments_Destroy(&runtime->attachments);
        runtime->receivers = RpgReceivers_Default();
    }
    RpgStageAuthority_Enter(RPG_STAGE_AUTHORITY_EDITOR_STATIC);
}
