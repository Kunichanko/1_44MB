// 依存する自プロジェクト内ファイル: rpg_button_event.h, rpg_preview_event.h, rpg_stage.h
// 役割: シグナルで一時的に1マスへ縮む2マス特殊ブロックの保存・反応・回転を管理する。
#ifndef RPG_SIGNAL_BLOCK_H
#define RPG_SIGNAL_BLOCK_H

#include "rpg_button_event.h"
#include "rpg_preview_event.h"
#include "rpg_stage.h"

enum { RPG_SIGNAL_BLOCK_MAX_COUNT = 64, RPG_PREVIEW_TARGET_ALL = -1, RPG_PREVIEW_TARGET_SIGNAL_BLOCK_BASE = 1000 };
typedef struct RpgSignalBlock {
    int row;
    int column;
    float duration;
    // true は通常時2マス、false は通常時1マス。シグナル中は必ず反対の形になる。
    bool startsExpanded;
    float activeRemaining;
    float previewRemaining;
} RpgSignalBlock;
typedef struct RpgSignalBlocks {
    RpgSignalBlock entries[RPG_SIGNAL_BLOCK_MAX_COUNT];
    int count;
    unsigned int lastSignalSequence;
} RpgSignalBlocks;

RpgSignalBlocks RpgSignalBlocks_Default(void);
bool RpgSignalBlocks_Load(const char *filePath, RpgSignalBlocks *blocks);
bool RpgSignalBlocks_Save(const char *filePath, const RpgSignalBlocks *blocks);
bool RpgSignalBlocks_Add(RpgSignalBlocks *blocks, int row, int column);
int RpgSignalBlocks_FindAtCell(const RpgSignalBlocks *blocks, const RpgStage *stage, int row, int column);
bool RpgSignalBlocks_Rotate(RpgSignalBlocks *blocks, RpgStage *stage, int index);
bool RpgSignalBlocks_Move(RpgSignalBlocks *blocks, int index, int row, int column);
bool RpgSignalBlocks_SetStartsExpanded(RpgSignalBlocks *blocks, RpgStage *stage, int index,
                                       bool startsExpanded);
void RpgSignalBlocks_RemoveBroken(RpgSignalBlocks *blocks, const RpgStage *stage);
void RpgSignalBlocks_Update(RpgSignalBlocks *blocks, RpgStage *stage,
                            const RpgButtonEvent *signal, float deltaTime);
void RpgSignalBlocks_Preview(RpgSignalBlocks *blocks, RpgStage *stage, const RpgPreviewEvent *event);
void RpgSignalBlocks_EndPreviews(RpgSignalBlocks *blocks, RpgStage *stage);
void RpgSignalBlocks_DrawPreview(const RpgSignalBlocks *blocks, const RpgStage *stage,
                                 int firstColumn, int columnCount);

#endif
