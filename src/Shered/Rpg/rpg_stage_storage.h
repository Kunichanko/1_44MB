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
    RpgAreaEntryEvents areaEntryEvents;
    RpgInspect npcInspectData;
    RpgItems items;
    RpgWires wires;
    RpgReceivers receivers;
    RpgAttachments attachments;
    RpgSignalBlocks signalBlocks;
    RpgMapEvents mapEvents;
} RpgStageData;

/* 保存先の役割。EDITOR は設計情報、GAME_PACKAGE は実行ファイルだけで使う配布済み静的情報。 */
typedef enum RpgStageStorageDomain {
    RPG_STAGE_STORAGE_SETTINGS = 0,
    RPG_STAGE_STORAGE_GAME_PACKAGE
} RpgStageStorageDomain;

typedef enum RpgStageRuntimeKind {
    RPG_STAGE_RUNTIME_GAME = 0,
    RPG_STAGE_RUNTIME_EDITOR
} RpgStageRuntimeKind;

void RpgStageStorage_SetDomain(RpgStageStorageDomain domain);
RpgStageStorageDomain RpgStageStorage_GetDomain(void);
bool RpgStageStorage_GetRuntimePath(int stageNumber, RpgStageRuntimeKind kind, char *path, int size);
/* ビルド時に生成した実行用構成を保存・復元する。Settings / stage.package は参照しない。 */
bool RpgStageStorage_SaveRuntimeState(int stageNumber, const RpgStageData *data);
bool RpgStageStorage_LoadRuntimeState(int stageNumber, RpgStageData *data);
bool RpgStageStorage_PublishStage(int stageNumber);
bool RpgStageStorage_PublishCatalog(const RpgStageCatalog *catalog);
void RpgStageStorage_ClearPackagedStaticStage(int stageNumber);

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
/* StageN/build配下のゲーム内Folder用実フォルダを、名前だけを入力として安全に管理する。 */
bool RpgStageStorage_CreateBuildFolder(int stageNumber, const char *name, char *path, int size);
bool RpgStageStorage_RenameBuildFolder(int stageNumber, const char *oldPath, const char *name,
                                       char *newPath, int newPathSize);
/* Fileオブジェクト用に、選択元をStageN/build/reference_filesへコピーして実行時パスを返す。 */
bool RpgStageStorage_CopyReferenceFileToBuild(int stageNumber, int row, int column,
                                              const char *sourcePath, char *copiedPath, int copiedPathSize);
bool RpgStageStorage_RepairReferenceFileCopies(int stageNumber, RpgStage *stage);
/* 同じステージのreference_files配下にある、以前のFileオブジェクト用コピーだけを削除する。 */
void RpgStageStorage_RemoveReferenceFileCopy(int stageNumber, const char *copiedPath);

#endif
