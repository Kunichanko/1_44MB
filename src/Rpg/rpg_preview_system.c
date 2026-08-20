// 依存する自プロジェクト内ファイル: rpg_preview_system.h
// 役割: プレビュー反応関数の登録と、共通通知の配信を実装する。
#include "rpg_preview_system.h"

#include <stddef.h>

RpgPreviewSystem RpgPreviewSystem_Default(void) { return (RpgPreviewSystem){ 0 }; }

bool RpgPreviewSystem_Register(RpgPreviewSystem *system, RpgPreviewReaction function, void *context)
{
    if (system == NULL || function == NULL || system->count >= RPG_PREVIEW_MAX_REACTIONS) return false;
    system->entries[system->count++] = (RpgPreviewReactionEntry){ function, context };
    return true;
}

void RpgPreviewSystem_Trigger(RpgPreviewSystem *system, int target)
{
    if (system == NULL) return;
    RpgPreviewEvent event = { .target = target };
    for (int index = 0; index < system->count; index++)
        system->entries[index].function(system->entries[index].context, &event);
}

bool RpgPreviewSystem_Dispatch(RpgPreviewSystem *system, const RpgPreviewEvent *event)
{
    if (system == NULL || !RpgPreviewEvent_Consume(event, &system->lastConsumedSequence)) return false;
    RpgPreviewSystem_Trigger(system, event->target);
    return true;
}
