// 役割: build の通常マス保存方式を選択し、方式ごとの生成処理を差し替える。
// 依存する自プロジェクト内ファイル: rpg_stage.h
#ifndef RPG_BUILD_CELL_STORAGE_H
#define RPG_BUILD_CELL_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "rpg_stage.h"
#include "rpg_grid_path.h"

typedef enum RpgBuildCellStorageMode {
    RPG_BUILD_CELL_STORAGE_COMPACT,
    RPG_BUILD_CELL_STORAGE_FOLDERS
} RpgBuildCellStorageMode;

/* 実ファイルの場所と通知は object_folder 側に残し、保存方式はこの小さな境界だけを使う。 */
typedef struct RpgBuildCellStorageBackend {
    void *context;
    bool (*writeCellFolder)(void *context, RpgGridCell cell, int blockType);
    bool (*getBuildFilePath)(void *context, const char *fileName, char *path, size_t pathSize);
    void (*notifyBuildFileChanged)(void *context, const char *path);
} RpgBuildCellStorageBackend;

RpgBuildCellStorageMode RpgBuildCellStorage_LoadMode(void);
bool RpgBuildCellStorage_SaveMode(RpgBuildCellStorageMode mode);
void RpgBuildCellStorage_SetMode(RpgBuildCellStorageMode mode);
RpgBuildCellStorageMode RpgBuildCellStorage_GetMode(void);
const char *RpgBuildCellStorage_GetModeName(RpgBuildCellStorageMode mode);

bool RpgBuildCellStorage_Create(const RpgStage *stage, int startMapIndex,
                                const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_CreatePreview(const RpgStage *stage, int startMapIndex,
                                       const RpgBuildCellStorageBackend *backend);
void RpgBuildCellStorage_Update(const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_EnsureCell(RpgGridCell cell, int blockType,
                                    const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_UsesMetadataForBlock(int blockType);
bool RpgBuildCellStorage_IsMetadataFile(const char *fileName);
bool RpgBuildCellStorage_ReadAvailability(bool available[RPG_STAGE_ROWS][RPG_STAGE_WORLD_COLUMNS],
                                          const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_Extract(RpgGridCell cell, int *blockType,
                                 const RpgBuildCellStorageBackend *backend);
bool RpgBuildCellStorage_Restore(RpgGridCell cell, int blockType,
                                 const RpgBuildCellStorageBackend *backend);

#endif
