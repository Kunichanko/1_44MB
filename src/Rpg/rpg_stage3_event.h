// 依存する自プロジェクト内ファイル: rpg_dialogue.h
#ifndef RPG_STAGE3_EVENT_H
#define RPG_STAGE3_EVENT_H

#include <stdbool.h>

#include "rpg_dialogue.h"

typedef struct RpgStage3Event {
    bool enabled;
    RpgDialogue dialogue;
} RpgStage3Event;

RpgStage3Event RpgStage3Event_Default(void);
bool RpgStage3Event_Load(const char *filePath, RpgStage3Event *event);
bool RpgStage3Event_Save(const char *filePath, const RpgStage3Event *event);

#endif
// 役割: ステージ3導入イベントの構造と保存 API を宣言する。
