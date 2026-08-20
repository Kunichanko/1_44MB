// 依存する自プロジェクト内ファイル: rpg_preview_event.h
// 役割: プレビュー対応機能の反応関数を登録し、共通通知で一回だけ呼び出す。
#ifndef RPG_PREVIEW_SYSTEM_H
#define RPG_PREVIEW_SYSTEM_H

#include "rpg_preview_event.h"

enum { RPG_PREVIEW_MAX_REACTIONS = 16 };
typedef void (*RpgPreviewReaction)(void *context, const RpgPreviewEvent *event);
typedef struct RpgPreviewReactionEntry { RpgPreviewReaction function; void *context; } RpgPreviewReactionEntry;
typedef struct RpgPreviewSystem {
    RpgPreviewReactionEntry entries[RPG_PREVIEW_MAX_REACTIONS];
    int count;
    unsigned int lastConsumedSequence;
} RpgPreviewSystem;

RpgPreviewSystem RpgPreviewSystem_Default(void);
bool RpgPreviewSystem_Register(RpgPreviewSystem *system, RpgPreviewReaction function, void *context);
void RpgPreviewSystem_Trigger(RpgPreviewSystem *system, int target);
bool RpgPreviewSystem_Dispatch(RpgPreviewSystem *system, const RpgPreviewEvent *event);

#endif
