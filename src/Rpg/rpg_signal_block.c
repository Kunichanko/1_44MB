// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_signal_block.h
// 役割: シグナル伸縮ブロックの実際の縮小、プレビュー、保存形式を実装する。
#include "rpg_signal_block.h"

#include "rpg_block_inventory.h"

#include <stdio.h>
#include <string.h>

static bool IsInside(int row, int column)
{ return row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS; }

typedef enum RpgSignalDirection { SIGNAL_RIGHT, SIGNAL_DOWN, SIGNAL_LEFT, SIGNAL_UP } RpgSignalDirection;

static RpgSignalDirection GetDirection(const RpgStage *stage, const RpgSignalBlock *block)
{
    int type = stage->blocks[block->row][block->column];
    if (type == RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL) return SIGNAL_DOWN;
    if (type == RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT) return SIGNAL_LEFT;
    if (type == RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP) return SIGNAL_UP;
    return SIGNAL_RIGHT;
}

static int GetPartRow(const RpgSignalBlock *block, RpgSignalDirection direction)
{ return block->row + (direction == SIGNAL_DOWN ? 1 : direction == SIGNAL_UP ? -1 : 0); }
static int GetPartColumn(const RpgSignalBlock *block, RpgSignalDirection direction)
{ return block->column + (direction == SIGNAL_RIGHT ? 1 : direction == SIGNAL_LEFT ? -1 : 0); }
static int GetRootType(RpgSignalDirection direction)
{
    return direction == SIGNAL_DOWN ? RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL :
           direction == SIGNAL_LEFT ? RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT :
           direction == SIGNAL_UP ? RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP : RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL;
}
static int GetPartType(RpgSignalDirection direction)
{
    return direction == SIGNAL_DOWN ? RPG_BLOCK_SIGNAL_SHRINK_PART_VERTICAL :
           direction == SIGNAL_LEFT ? RPG_BLOCK_SIGNAL_SHRINK_PART_LEFT :
           direction == SIGNAL_UP ? RPG_BLOCK_SIGNAL_SHRINK_PART_UP : RPG_BLOCK_SIGNAL_SHRINK_PART_HORIZONTAL;
}

// 通常状態とシグナル中の一時状態のどちらにも使う、2マス目の共通切替処理。
static void SetExpanded(RpgStage *stage, const RpgSignalBlock *block, bool isExpanded)
{
    RpgSignalDirection direction = GetDirection(stage, block);
    int partRow = GetPartRow(block, direction);
    int partColumn = GetPartColumn(block, direction);
    if (!IsInside(partRow, partColumn)) return;
    if (isExpanded) {
        if (stage->blocks[partRow][partColumn] == 0 ||
            stage->blocks[partRow][partColumn] == GetPartType(direction))
            stage->blocks[partRow][partColumn] = GetPartType(direction);
    } else if (stage->blocks[partRow][partColumn] == GetPartType(direction)) {
        stage->blocks[partRow][partColumn] = 0;
    }
}

static void RestoreStandardState(RpgStage *stage, const RpgSignalBlock *block)
{
    if (block->activeRemaining <= 0.0f && block->previewRemaining <= 0.0f)
        SetExpanded(stage, block, block->startsExpanded);
}

RpgSignalBlocks RpgSignalBlocks_Default(void) { return (RpgSignalBlocks){ 0 }; }

bool RpgSignalBlocks_Load(const char *filePath, RpgSignalBlocks *blocks)
{
    FILE *file = fopen(filePath, "r");
    RpgSignalBlocks loaded = RpgSignalBlocks_Default();
    char format[8] = { 0 };
    if (file == NULL) return false;
    if (fscanf(file, "%7s %d", format, &loaded.count) != 2 ||
        (strcmp(format, "v1") != 0 && strcmp(format, "v2") != 0) ||
        loaded.count < 0 || loaded.count > RPG_SIGNAL_BLOCK_MAX_COUNT) {
        fclose(file); return false;
    }
    for (int index = 0; index < loaded.count; index++) {
        RpgSignalBlock *block = &loaded.entries[index];
        int startsExpanded = 1;
        if (fscanf(file, "%d %d %f %d", &block->row, &block->column, &block->duration, &startsExpanded) != 4 ||
            !IsInside(block->row, block->column) || block->duration < 0.1f || block->duration > 30.0f) {
            fclose(file); return false;
        }
        // v1 の4列目は廃止済みプレビュー設定なので、旧保存は従来どおり伸びた状態に移行する。
        block->startsExpanded = strcmp(format, "v1") == 0 || startsExpanded != 0;
    }
    fclose(file); *blocks = loaded; return true;
}

bool RpgSignalBlocks_Save(const char *filePath, const RpgSignalBlocks *blocks)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "v2 %d\n", blocks->count);
    for (int index = 0; index < blocks->count; index++) {
        const RpgSignalBlock *block = &blocks->entries[index];
        fprintf(file, "%d %d %.2f %d\n", block->row, block->column, block->duration,
                block->startsExpanded ? 1 : 0);
    }
    return fclose(file) == 0;
}

bool RpgSignalBlocks_Add(RpgSignalBlocks *blocks, int row, int column)
{
    if (blocks == NULL || blocks->count >= RPG_SIGNAL_BLOCK_MAX_COUNT) return false;
    blocks->entries[blocks->count++] = (RpgSignalBlock){ .row = row, .column = column,
                                                           .duration = 2.0f, .startsExpanded = true };
    return true;
}

int RpgSignalBlocks_FindAtCell(const RpgSignalBlocks *blocks, const RpgStage *stage, int row, int column)
{
    if (blocks == NULL || stage == NULL) return -1;
    for (int index = 0; index < blocks->count; index++) {
        const RpgSignalBlock *block = &blocks->entries[index];
        RpgSignalDirection direction = GetDirection(stage, block);
        if ((row == block->row && column == block->column) ||
            (row == GetPartRow(block, direction) && column == GetPartColumn(block, direction))) return index;
    }
    return -1;
}

bool RpgSignalBlocks_Rotate(RpgSignalBlocks *blocks, RpgStage *stage, int index)
{
    if (blocks == NULL || stage == NULL || index < 0 || index >= blocks->count) return false;
    RpgSignalBlock *block = &blocks->entries[index];
    if (block->activeRemaining > 0.0f || block->previewRemaining > 0.0f) return false;
    RpgSignalDirection oldDirection = GetDirection(stage, block);
    RpgSignalDirection newDirection = (RpgSignalDirection)((oldDirection + 1) % 4);
    int oldPartRow = GetPartRow(block, oldDirection), oldPartColumn = GetPartColumn(block, oldDirection);
    int newPartRow = GetPartRow(block, newDirection), newPartColumn = GetPartColumn(block, newDirection);
    if (!IsInside(newPartRow, newPartColumn) ||
        ((newPartRow != oldPartRow || newPartColumn != oldPartColumn) &&
         stage->blocks[newPartRow][newPartColumn] != 0)) return false;
    if (IsInside(oldPartRow, oldPartColumn) &&
        stage->blocks[oldPartRow][oldPartColumn] == GetPartType(oldDirection))
        stage->blocks[oldPartRow][oldPartColumn] = 0;
    stage->blocks[block->row][block->column] = GetRootType(newDirection);
    if (block->startsExpanded) stage->blocks[newPartRow][newPartColumn] = GetPartType(newDirection);
    return true;
}

bool RpgSignalBlocks_Move(RpgSignalBlocks *blocks, int index, int row, int column)
{
    if (blocks == NULL || index < 0 || index >= blocks->count || !IsInside(row, column)) return false;
    blocks->entries[index].row = row;
    blocks->entries[index].column = column;
    return true;
}

bool RpgSignalBlocks_SetStartsExpanded(RpgSignalBlocks *blocks, RpgStage *stage, int index,
                                       bool startsExpanded)
{
    if (blocks == NULL || stage == NULL || index < 0 || index >= blocks->count) return false;
    RpgSignalBlock *block = &blocks->entries[index];
    if (block->activeRemaining > 0.0f || block->previewRemaining > 0.0f) return false;
    if (block->startsExpanded == startsExpanded) return true;
    block->startsExpanded = startsExpanded;
    SetExpanded(stage, block, startsExpanded);
    return true;
}

void RpgSignalBlocks_RemoveBroken(RpgSignalBlocks *blocks, const RpgStage *stage)
{
    if (blocks == NULL || stage == NULL) return;
    for (int index = blocks->count - 1; index >= 0; index--) {
        const RpgSignalBlock *block = &blocks->entries[index];
        RpgSignalDirection direction = GetDirection(stage, block);
        int partRow = GetPartRow(block, direction), partColumn = GetPartColumn(block, direction);
        bool rootValid = IsInside(block->row, block->column) &&
                         stage->blocks[block->row][block->column] == GetRootType(direction);
        bool isTemporarilyOpposite = block->activeRemaining > 0.0f || block->previewRemaining > 0.0f;
        bool partValid = isTemporarilyOpposite ||
                         (block->startsExpanded
                            ? (IsInside(partRow, partColumn) && stage->blocks[partRow][partColumn] == GetPartType(direction))
                            : (!IsInside(partRow, partColumn) || stage->blocks[partRow][partColumn] == 0));
        if (rootValid && partValid) continue;
        for (int move = index; move < blocks->count - 1; move++) blocks->entries[move] = blocks->entries[move + 1];
        blocks->count--;
    }
}

static void Activate(RpgSignalBlocks *blocks, RpgStage *stage, int index)
{
    RpgSignalBlock *block = &blocks->entries[index];
    SetExpanded(stage, block, !block->startsExpanded);
    block->activeRemaining = block->duration;
}

void RpgSignalBlocks_Update(RpgSignalBlocks *blocks, RpgStage *stage,
                            const RpgButtonEvent *signal, float deltaTime)
{
    if (blocks == NULL || stage == NULL) return;
    if (RpgButtonEvent_Consume(signal, &blocks->lastSignalSequence))
        for (int index = 0; index < blocks->count; index++) Activate(blocks, stage, index);
    for (int index = 0; index < blocks->count; index++) {
        RpgSignalBlock *block = &blocks->entries[index];
        if (block->previewRemaining > 0.0f) {
            block->previewRemaining -= deltaTime;
            if (block->previewRemaining <= 0.0f) {
                block->previewRemaining = 0.0f;
                RestoreStandardState(stage, block);
            }
        }
        if (block->activeRemaining <= 0.0f) continue;
        block->activeRemaining -= deltaTime;
        if (block->activeRemaining > 0.0f) continue;
        block->activeRemaining = 0.0f;
        RestoreStandardState(stage, block);
    }
}

void RpgSignalBlocks_Preview(RpgSignalBlocks *blocks, RpgStage *stage, const RpgPreviewEvent *event)
{
    if (blocks == NULL || stage == NULL || event == NULL) return;
    for (int index = 0; index < blocks->count; index++) {
        if (event->target != RPG_PREVIEW_TARGET_ALL &&
            event->target != RPG_PREVIEW_TARGET_SIGNAL_BLOCK_BASE + index) continue;
        RpgSignalBlock *block = &blocks->entries[index];
        SetExpanded(stage, block, !block->startsExpanded);
        block->previewRemaining = block->duration;
    }
}

void RpgSignalBlocks_EndPreviews(RpgSignalBlocks *blocks, RpgStage *stage)
{
    if (blocks == NULL || stage == NULL) return;
    for (int index = 0; index < blocks->count; index++) {
        RpgSignalBlock *block = &blocks->entries[index];
        if (block->previewRemaining <= 0.0f) continue;
        block->previewRemaining = 0.0f;
        RestoreStandardState(stage, block);
    }
}

void RpgSignalBlocks_DrawPreview(const RpgSignalBlocks *blocks, const RpgStage *stage,
                                 int firstColumn, int columnCount)
{
    if (blocks == NULL || stage == NULL) return;
    for (int index = 0; index < blocks->count; index++) {
        const RpgSignalBlock *block = &blocks->entries[index];
        if (block->previewRemaining <= 0.0f || block->column < firstColumn || block->column >= firstColumn + columnCount) continue;
        RpgSignalDirection direction = GetDirection(stage, block);
        int partRow = GetPartRow(block, direction), partColumn = GetPartColumn(block, direction);
        Rectangle changed = { (partColumn - firstColumn) * RPG_STAGE_TILE_SIZE, partRow * RPG_STAGE_TILE_SIZE,
                             RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        DrawRectangleLinesEx(changed, 2.0f, SKYBLUE);
        DrawText("PREVIEW", (int)changed.x + 2, (int)changed.y + 18, 10, DARKBLUE);
    }
}
