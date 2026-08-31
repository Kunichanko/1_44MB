// 依存する自プロジェクト内ファイル: rpg_dialogue.h, rpg_inspect.h
#ifndef RPG_STAGE3_EVENT_H
#define RPG_STAGE3_EVENT_H

#include <stdbool.h>

#include "rpg_dialogue.h"
#include "rpg_inspect.h"
#include "rpg_stage.h"

// 依存関係を更新: エリアごとのイベント数をステージ定数へ合わせるため rpg_stage.h を参照する。

typedef struct RpgStage3Event {
    bool enabled;
    /* 旧会話データは読み込み移行用に保持し、実行・編集はinspectへ統一する。 */
    RpgDialogue dialogue;
    RpgInspect inspect;
} RpgStage3Event;

/* ステージ入場・エリア初回入場で共用するイベント群。既存名は旧データ互換のため残す。 */
typedef struct RpgAreaEntryEvents {
    RpgStage3Event entries[RPG_STAGE_MAP_COUNT];
} RpgAreaEntryEvents;

RpgStage3Event RpgStage3Event_Default(void);
bool RpgStage3Event_Load(const char *filePath, RpgStage3Event *event);
bool RpgStage3Event_Save(const char *filePath, const RpgStage3Event *event);
/* 大きなエリアイベント配列を戻り値にせず、呼び出し元の領域へ直接初期化する。 */
void RpgAreaEntryEvents_Initialize(RpgAreaEntryEvents *events);
bool RpgAreaEntryEvents_Load(const char *filePath, const RpgStage *stage, RpgAreaEntryEvents *events);
bool RpgAreaEntryEvents_Save(const char *filePath, const RpgStage *stage, const RpgAreaEntryEvents *events);

#endif
// 役割: ステージ3導入イベントの構造と保存 API を宣言する。
