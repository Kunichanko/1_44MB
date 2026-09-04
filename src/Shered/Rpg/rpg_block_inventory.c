// 依存する自プロジェクト内ファイル: rpg_block_inventory.h
#include "rpg_block_inventory.h"

#include <stddef.h>

static const RpgBlockInventory inventories[] = {
    { "Earth", { 1, RPG_BLOCK_HOLE_VERTICAL, RPG_BLOCK_HOLE_HORIZONTAL,
                   RPG_BLOCK_ONE_WAY_PLATFORM, RPG_BLOCK_METAL, RPG_BLOCK_PUSH_BLOCK }, 6, false, false },
    /* 既存ステージとの互換性を保つため定義は残し、選択パレットだけを必要な効果へ絞る。 */
    { "Effect", { RPG_BLOCK_DOOR_CLOSED_TOP, RPG_BLOCK_KEY_DOOR_CLOSED_TOP,
                    RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL, RPG_BLOCK_EFFECT_MAGNET_OFF }, 4, false, false },
    { "Item Property", { RPG_BLOCK_PROPERTY_ITEM, RPG_BLOCK_PROPERTY_WIRE,
                           RPG_BLOCK_PROPERTY_MAP_EVENT }, 3, true, false },
    { "Attachment Edge", { RPG_BLOCK_ATTACHMENT_DATA_BUTTON,
                             RPG_BLOCK_PROPERTY_RECEIVER }, 2, false, true },
    { "Attachment Object", { RPG_BLOCK_ATTACHMENT_RADIO_EMITTER,
                               RPG_BLOCK_ATTACHMENT_SAVE_FLAG }, 2, false, true },
    { "Reference Object", { RPG_BLOCK_REFERENCE_FILE, RPG_BLOCK_REFERENCE_FOLDER, RPG_BLOCK_IMAGE_OBJECT }, 3, false, false }
};

// 各特殊ブロックは先頭マスからの相対座標で占有形状を定義する。
static const RpgEffectShape effectShapes[] = {
    { RPG_BLOCK_EFFECT_BOUNCE, { { 0, 0, RPG_BLOCK_EFFECT_BOUNCE } }, 1 },
    { RPG_BLOCK_EFFECT_SLOW, { { 0, 0, RPG_BLOCK_EFFECT_SLOW } }, 1 },
    { RPG_BLOCK_EFFECT_WIDE_BOUNCE, {
        { 0, 0, RPG_BLOCK_EFFECT_WIDE_BOUNCE }, { 1, 0, RPG_BLOCK_EFFECT_WIDE_BOUNCE_PART }
    }, 2 },
    { RPG_BLOCK_EFFECT_CORNER_BOUNCE, {
        { 0, 0, RPG_BLOCK_EFFECT_CORNER_BOUNCE }, { 1, 0, RPG_BLOCK_EFFECT_CORNER_BOUNCE_RIGHT },
        { 0, 1, RPG_BLOCK_EFFECT_CORNER_BOUNCE_DOWN }
    }, 3 },
    { RPG_BLOCK_DOOR_CLOSED_TOP, {
        { 0, 0, RPG_BLOCK_DOOR_CLOSED_TOP }, { 0, 1, RPG_BLOCK_DOOR_CLOSED_MIDDLE },
        { 0, 2, RPG_BLOCK_DOOR_CLOSED_BOTTOM }
    }, 3 },
    { RPG_BLOCK_DOOR_OPEN_TOP, {
        { 0, 0, RPG_BLOCK_DOOR_OPEN_TOP }, { 0, 1, RPG_BLOCK_DOOR_OPEN_MIDDLE },
        { 0, 2, RPG_BLOCK_DOOR_OPEN_BOTTOM }
    }, 3 },
    { RPG_BLOCK_KEY_DOOR_CLOSED_TOP, {
        { 0, 0, RPG_BLOCK_KEY_DOOR_CLOSED_TOP }, { 0, 1, RPG_BLOCK_KEY_DOOR_CLOSED_MIDDLE },
        { 0, 2, RPG_BLOCK_KEY_DOOR_CLOSED_BOTTOM }
    }, 3 },
    { RPG_BLOCK_KEY_DOOR_OPEN_TOP, {
        { 0, 0, RPG_BLOCK_KEY_DOOR_OPEN_TOP }, { 0, 1, RPG_BLOCK_KEY_DOOR_OPEN_MIDDLE },
        { 0, 2, RPG_BLOCK_KEY_DOOR_OPEN_BOTTOM }
    }, 3 },
    { RPG_BLOCK_EFFECT_BUTTON, { { 0, 0, RPG_BLOCK_EFFECT_BUTTON } }, 1 },
    { RPG_BLOCK_EFFECT_MAGNET_OFF, { { 0, 0, RPG_BLOCK_EFFECT_MAGNET_OFF } }, 1 },
    { RPG_BLOCK_EFFECT_MAGNET_ON, { { 0, 0, RPG_BLOCK_EFFECT_MAGNET_ON } }, 1 }
    , { RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL, {
        { 0, 0, RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL }, { 1, 0, RPG_BLOCK_SIGNAL_SHRINK_PART_HORIZONTAL }
    }, 2 }
    , { RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL, {
        { 0, 0, RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL }, { 0, 1, RPG_BLOCK_SIGNAL_SHRINK_PART_VERTICAL }
    }, 2 }
    , { RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT, {
        { 0, 0, RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT }, { -1, 0, RPG_BLOCK_SIGNAL_SHRINK_PART_LEFT }
    }, 2 }
    , { RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP, {
        { 0, 0, RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP }, { 0, -1, RPG_BLOCK_SIGNAL_SHRINK_PART_UP }
    }, 2 }
};

int RpgBlockInventory_Count(void) { return (int)(sizeof(inventories) / sizeof(inventories[0])); }
const RpgBlockInventory *RpgBlockInventory_Get(int index)
{
    return index >= 0 && index < RpgBlockInventory_Count() ? &inventories[index] : &inventories[0];
}

bool RpgBlockInventory_IsEffectBlock(int blockType)
{
    return RpgBlockInventory_GetEffectShape(blockType) != NULL;
}

bool RpgBlockInventory_IsEffectBlockPart(int blockType)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
    return shape != NULL && blockType != shape->rootType;
}

bool RpgBlockInventory_IsBounceEffect(int blockType)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
    return shape != NULL && (shape->rootType == RPG_BLOCK_EFFECT_BOUNCE ||
                             shape->rootType == RPG_BLOCK_EFFECT_WIDE_BOUNCE ||
                             shape->rootType == RPG_BLOCK_EFFECT_CORNER_BOUNCE);
}

bool RpgBlockInventory_IsButtonEffect(int blockType)
{
    return RpgBlockInventory_GetEffectRootType(blockType) == RPG_BLOCK_EFFECT_BUTTON;
}

bool RpgBlockInventory_IsHoleBlock(int blockType)
{
    return blockType == RPG_BLOCK_HOLE_VERTICAL || blockType == RPG_BLOCK_HOLE_HORIZONTAL;
}

bool RpgBlockInventory_IsReferenceObject(int blockType)
{
    return blockType == RPG_BLOCK_REFERENCE_FILE || blockType == RPG_BLOCK_REFERENCE_FOLDER;
}

bool RpgBlockInventory_IsReferenceFolder(int blockType)
{
    return blockType == RPG_BLOCK_REFERENCE_FOLDER;
}

bool RpgBlockInventory_IsDoorBlock(int blockType)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
    return shape != NULL && (shape->rootType == RPG_BLOCK_DOOR_CLOSED_TOP ||
                             shape->rootType == RPG_BLOCK_DOOR_OPEN_TOP ||
                             shape->rootType == RPG_BLOCK_KEY_DOOR_CLOSED_TOP ||
                             shape->rootType == RPG_BLOCK_KEY_DOOR_OPEN_TOP);
}

bool RpgBlockInventory_IsDoorOpen(int blockType)
{
    int rootType = RpgBlockInventory_GetEffectRootType(blockType);
    return rootType == RPG_BLOCK_DOOR_OPEN_TOP || rootType == RPG_BLOCK_KEY_DOOR_OPEN_TOP;
}

bool RpgBlockInventory_IsKeyDoorBlock(int blockType)
{
    int rootType = RpgBlockInventory_GetEffectRootType(blockType);
    return rootType == RPG_BLOCK_KEY_DOOR_CLOSED_TOP || rootType == RPG_BLOCK_KEY_DOOR_OPEN_TOP;
}

bool RpgBlockInventory_IsKeyDoorOpen(int blockType)
{
    return RpgBlockInventory_GetEffectRootType(blockType) == RPG_BLOCK_KEY_DOOR_OPEN_TOP;
}

bool RpgBlockInventory_IsSignalShrinkBlock(int blockType)
{
    int rootType = RpgBlockInventory_GetEffectRootType(blockType);
    return rootType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL ||
           rootType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL ||
           rootType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT ||
           rootType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP;
}

bool RpgBlockInventory_IsAttachment(int blockType)
{
    return blockType == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER ||
           blockType == RPG_BLOCK_ATTACHMENT_DATA_BUTTON || blockType == RPG_BLOCK_ATTACHMENT_SAVE_FLAG;
}

bool RpgBlockInventory_IsCellAttachment(int blockType)
{
    return blockType == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER ||
           blockType == RPG_BLOCK_ATTACHMENT_SAVE_FLAG;
}

bool RpgBlockInventory_IsMapEventProperty(int blockType)
{
    return blockType == RPG_BLOCK_PROPERTY_MAP_EVENT;
}

bool RpgBlockInventory_IsOneWayPlatform(int blockType)
{
    return blockType == RPG_BLOCK_ONE_WAY_PLATFORM;
}

bool RpgBlockInventory_IsMagnetBlock(int blockType)
{
    int rootType = RpgBlockInventory_GetEffectRootType(blockType);
    return rootType == RPG_BLOCK_EFFECT_MAGNET_OFF || rootType == RPG_BLOCK_EFFECT_MAGNET_ON;
}

bool RpgBlockInventory_IsMagnetActive(int blockType)
{
    return RpgBlockInventory_GetEffectRootType(blockType) == RPG_BLOCK_EFFECT_MAGNET_ON;
}

bool RpgBlockInventory_IsMetalBlock(int blockType)
{
    return blockType == RPG_BLOCK_METAL;
}

bool RpgBlockInventory_IsPushBlock(int blockType)
{
    return blockType == RPG_BLOCK_PUSH_BLOCK;
}

bool RpgBlockInventory_IsOrdinaryBlock(int blockType)
{
    return (blockType >= 1 && blockType <= 10) ||
           blockType == RPG_BLOCK_HOLE_VERTICAL || blockType == RPG_BLOCK_HOLE_HORIZONTAL ||
           RpgBlockInventory_IsOneWayPlatform(blockType) || RpgBlockInventory_IsMetalBlock(blockType) ||
           RpgBlockInventory_IsPushBlock(blockType);
}

int RpgBlockInventory_GetEffectRootType(int blockType)
{
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
    return shape != NULL ? shape->rootType : blockType;
}

const RpgEffectShape *RpgBlockInventory_GetEffectShape(int blockType)
{
    for (int shapeIndex = 0; shapeIndex < (int)(sizeof(effectShapes) / sizeof(effectShapes[0])); shapeIndex++)
        for (int cellIndex = 0; cellIndex < effectShapes[shapeIndex].cellCount; cellIndex++)
            if (effectShapes[shapeIndex].cells[cellIndex].blockType == blockType) return &effectShapes[shapeIndex];
    return NULL;
}

const RpgEffectShape *RpgBlockInventory_GetDoorShape(bool isOpen)
{
    return RpgBlockInventory_GetEffectShape(isOpen ? RPG_BLOCK_DOOR_OPEN_TOP : RPG_BLOCK_DOOR_CLOSED_TOP);
}

const RpgEffectShape *RpgBlockInventory_GetKeyDoorShape(bool isOpen)
{
    return RpgBlockInventory_GetEffectShape(isOpen ? RPG_BLOCK_KEY_DOOR_OPEN_TOP :
                                                    RPG_BLOCK_KEY_DOOR_CLOSED_TOP);
}
// 役割: ブロックパレット、特殊ブロックの分類、形状定義を提供する。
