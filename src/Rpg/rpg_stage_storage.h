// 依存する自プロジェクト内ファイル: rpg_attachment.h, rpg_dialogue.h, rpg_inspect.h, rpg_item.h, rpg_layout.h, rpg_map_event.h, rpg_receiver.h, rpg_signal_block.h, rpg_stage.h, rpg_stage3_event.h, rpg_wire.h
// 役割: ステージ番号ごとの設定フォルダ、ステージ一覧、ロード・セーブを一元管理する。
#ifndef RPG_STAGE_STORAGE_H
#define RPG_STAGE_STORAGE_H

#include "rpg_attachment.h"
#include "rpg_dialogue.h"
#include "rpg_inspect.h"
#include "rpg_item.h"
#include "rpg_layout.h"
#include "rpg_map_event.h"
#include "rpg_receiver.h"
#include "rpg_signal_block.h"
#include "rpg_stage.h"
#include "rpg_stage3_event.h"
#include "rpg_wire.h"

enum { RPG_STAGE_CATALOG_MAX_COUNT = 32, RPG_STAGE_NAME_LENGTH = 32, RPG_STAGE_PATH_LENGTH = 1200 };

typedef struct RpgStageCatalog {
    int numbers[RPG_STAGE_CATALOG_MAX_COUNT];
    int count;
    int currentNumber;
    int savedNumbers[RPG_STAGE_CATALOG_MAX_COUNT];
    int savedCount;
    int savedCurrentNumber;
    int deletedNumbers[RPG_STAGE_CATALOG_MAX_COUNT];
    int deletedCount;
} RpgStageCatalog;

typedef struct RpgStageData {
    RpgLayout layout;
    RpgStage stage;
    RpgDialogue dialogue;
    RpgStage3Event stage3Event;
    RpgInspect npcInspectData;
    RpgItems items;
    RpgWires wires;
    RpgReceivers receivers;
    RpgAttachments attachments;
    RpgSignalBlocks signalBlocks;
    RpgMapEvents mapEvents;
} RpgStageData;

bool RpgStageCatalog_Load(RpgStageCatalog *catalog);
bool RpgStageCatalog_Save(RpgStageCatalog *catalog);
void RpgStageCatalog_Revert(RpgStageCatalog *catalog);
bool RpgStageCatalog_IsDirty(const RpgStageCatalog *catalog);
int RpgStageCatalog_GetCurrentNumber(const RpgStageCatalog *catalog);
int RpgStageCatalog_GetNumberAt(const RpgStageCatalog *catalog, int index);
int RpgStageCatalog_FindIndex(const RpgStageCatalog *catalog, int stageNumber);
bool RpgStageCatalog_Select(RpgStageCatalog *catalog, int stageNumber);
int RpgStageCatalog_Add(RpgStageCatalog *catalog);
bool RpgStageCatalog_DeleteCurrent(RpgStageCatalog *catalog);
void RpgStageCatalog_GetName(int stageNumber, char *name, int size);

bool RpgStageStorage_LoadStage(int stageNumber, RpgStageData *data);
bool RpgStageStorage_SaveStage(int stageNumber, const RpgStageData *data);
bool RpgStageStorage_EnsureStageDirectory(int stageNumber);
bool RpgStageStorage_GetFilePath(int stageNumber, const char *fileName, char *path, int size);

#endif
