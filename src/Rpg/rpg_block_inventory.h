// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_BLOCK_INVENTORY_H
#define RPG_BLOCK_INVENTORY_H

#include <stdbool.h>

enum {
    RPG_BLOCK_INVENTORY_MAX_SLOTS = 10,
    RPG_BLOCK_EFFECT_MAX_SHAPE_CELLS = 8,
    RPG_BLOCK_EFFECT_BOUNCE = 11,
    RPG_BLOCK_EFFECT_SLOW = 12,
    RPG_BLOCK_EFFECT_WIDE_BOUNCE = 13,
    RPG_BLOCK_EFFECT_WIDE_BOUNCE_PART = 14,
    RPG_BLOCK_EFFECT_CORNER_BOUNCE = 15,
    RPG_BLOCK_EFFECT_CORNER_BOUNCE_RIGHT = 16,
    RPG_BLOCK_EFFECT_CORNER_BOUNCE_DOWN = 17,
    RPG_BLOCK_DOOR_CLOSED_TOP = 18,
    RPG_BLOCK_DOOR_CLOSED_MIDDLE = 19,
    RPG_BLOCK_DOOR_CLOSED_BOTTOM = 20,
    RPG_BLOCK_DOOR_OPEN_TOP = 21,
    RPG_BLOCK_DOOR_OPEN_MIDDLE = 22,
    RPG_BLOCK_DOOR_OPEN_BOTTOM = 23,
    RPG_BLOCK_EFFECT_BUTTON = 24,
    RPG_BLOCK_HOLE_VERTICAL = 25,
    RPG_BLOCK_HOLE_HORIZONTAL = 26,
    RPG_BLOCK_REFERENCE_FILE = 27,
    RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL = 28,
    RPG_BLOCK_SIGNAL_SHRINK_PART_HORIZONTAL = 29,
    RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL = 30,
    RPG_BLOCK_SIGNAL_SHRINK_PART_VERTICAL = 31,
    RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT = 32,
    RPG_BLOCK_SIGNAL_SHRINK_PART_LEFT = 33,
    RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP = 34,
    RPG_BLOCK_SIGNAL_SHRINK_PART_UP = 35,
    RPG_BLOCK_ATTACHMENT_RADIO_EMITTER = 200,
    RPG_BLOCK_ATTACHMENT_DATA_BUTTON = 201,
    RPG_BLOCK_ATTACHMENT_SAVE_FLAG = 202,
    RPG_BLOCK_PROPERTY_ITEM = 100,
    RPG_BLOCK_PROPERTY_WIRE = 101,
    RPG_BLOCK_PROPERTY_RECEIVER = 102,
    /* ビルド出力から対応フォルダが失われたマスを表す、実行時専用の壁。 */
    RPG_BLOCK_BUILD_MISSING = 300
};
typedef struct RpgBlockInventory { const char *name; int blockTypes[RPG_BLOCK_INVENTORY_MAX_SLOTS]; int count; bool isProperty; bool isAttachment; } RpgBlockInventory;
typedef struct RpgEffectShapeCell { int offsetX; int offsetY; int blockType; } RpgEffectShapeCell;
typedef struct RpgEffectShape { int rootType; RpgEffectShapeCell cells[RPG_BLOCK_EFFECT_MAX_SHAPE_CELLS]; int cellCount; } RpgEffectShape;

int RpgBlockInventory_Count(void);
const RpgBlockInventory *RpgBlockInventory_Get(int index);
bool RpgBlockInventory_IsEffectBlock(int blockType);
bool RpgBlockInventory_IsEffectBlockPart(int blockType);
bool RpgBlockInventory_IsBounceEffect(int blockType);
bool RpgBlockInventory_IsButtonEffect(int blockType);
bool RpgBlockInventory_IsHoleBlock(int blockType);
bool RpgBlockInventory_IsReferenceObject(int blockType);
bool RpgBlockInventory_IsDoorBlock(int blockType);
bool RpgBlockInventory_IsDoorOpen(int blockType);
bool RpgBlockInventory_IsSignalShrinkBlock(int blockType);
bool RpgBlockInventory_IsAttachment(int blockType);
bool RpgBlockInventory_IsCellAttachment(int blockType);
bool RpgBlockInventory_IsOrdinaryBlock(int blockType);
int RpgBlockInventory_GetEffectRootType(int blockType);
const RpgEffectShape *RpgBlockInventory_GetEffectShape(int blockType);
const RpgEffectShape *RpgBlockInventory_GetDoorShape(bool isOpen);
#endif
// 役割: ブロック種別・パレット・特殊形状の定義を宣言する。
