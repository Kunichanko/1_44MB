// 依存する自プロジェクト内ファイル: rpg_block_inventory.h
#include "rpg_block_inventory.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const RpgBlockInventory defaultInventories[] = {
    { "Earth", { 1, RPG_BLOCK_HOLE_VERTICAL, RPG_BLOCK_HOLE_HORIZONTAL,
                   RPG_BLOCK_ONE_WAY_PLATFORM, RPG_BLOCK_METAL, RPG_BLOCK_PUSH_BLOCK,
                   RPG_BLOCK_SOCKET_SIGNAL_SOLID, RPG_BLOCK_SOCKET_SIGNAL_ONE_WAY }, 8, false, false,
      RPG_BLOCK_INVENTORY_BORDER_WHITE },
    /* 既存ステージとの互換性を保つため定義は残し、選択パレットだけを必要な効果へ絞る。 */
    { "Effect", { RPG_BLOCK_DOOR_CLOSED_TOP, RPG_BLOCK_KEY_DOOR_CLOSED_TOP,
                    RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL, RPG_BLOCK_EFFECT_MAGNET_OFF }, 4, false, false,
      RPG_BLOCK_INVENTORY_BORDER_RED },
    /* Paths are owned by their representative block, never placed alone. */
    { "Item Property", { RPG_BLOCK_PROPERTY_ITEM, RPG_BLOCK_PROPERTY_MAP_EVENT,
                           RPG_BLOCK_PROPERTY_CONVEYOR }, 3, true, false,
      RPG_BLOCK_INVENTORY_BORDER_BLUE },
    { "Attachment Edge", { RPG_BLOCK_ATTACHMENT_DATA_BUTTON,
                             RPG_BLOCK_PROPERTY_RECEIVER }, 2, false, true,
      RPG_BLOCK_INVENTORY_BORDER_YELLOW },
    { "Attachment Object", { RPG_BLOCK_ATTACHMENT_RADIO_EMITTER,
                               RPG_BLOCK_ATTACHMENT_SAVE_FLAG, RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET }, 3, false, true,
      RPG_BLOCK_INVENTORY_BORDER_WHITE },
    { "Reference Object", { RPG_BLOCK_REFERENCE_FILE, RPG_BLOCK_REFERENCE_FOLDER, RPG_BLOCK_IMAGE_OBJECT }, 3, false, false,
      RPG_BLOCK_INVENTORY_BORDER_BLUE }
};

static RpgBlockInventory inventories[RPG_BLOCK_INVENTORY_MAX_PALETTES] = {
    { "Earth", { 1, RPG_BLOCK_HOLE_VERTICAL, RPG_BLOCK_HOLE_HORIZONTAL,
                   RPG_BLOCK_ONE_WAY_PLATFORM, RPG_BLOCK_METAL, RPG_BLOCK_PUSH_BLOCK,
                   RPG_BLOCK_SOCKET_SIGNAL_SOLID, RPG_BLOCK_SOCKET_SIGNAL_ONE_WAY }, 8, false, false,
      RPG_BLOCK_INVENTORY_BORDER_WHITE },
    { "Effect", { RPG_BLOCK_DOOR_CLOSED_TOP, RPG_BLOCK_KEY_DOOR_CLOSED_TOP,
                    RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL, RPG_BLOCK_EFFECT_MAGNET_OFF }, 4, false, false,
      RPG_BLOCK_INVENTORY_BORDER_RED },
    { "Item Property", { RPG_BLOCK_PROPERTY_ITEM, RPG_BLOCK_PROPERTY_MAP_EVENT,
                           RPG_BLOCK_PROPERTY_CONVEYOR }, 3, true, false,
      RPG_BLOCK_INVENTORY_BORDER_BLUE },
    { "Attachment Edge", { RPG_BLOCK_ATTACHMENT_DATA_BUTTON,
                             RPG_BLOCK_PROPERTY_RECEIVER }, 2, false, true,
      RPG_BLOCK_INVENTORY_BORDER_YELLOW },
    { "Attachment Object", { RPG_BLOCK_ATTACHMENT_RADIO_EMITTER,
                               RPG_BLOCK_ATTACHMENT_SAVE_FLAG, RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET }, 3, false, true,
      RPG_BLOCK_INVENTORY_BORDER_WHITE },
    { "Reference Object", { RPG_BLOCK_REFERENCE_FILE, RPG_BLOCK_REFERENCE_FOLDER, RPG_BLOCK_IMAGE_OBJECT }, 3, false, false,
      RPG_BLOCK_INVENTORY_BORDER_BLUE }
};
static int inventoryCount = (int)(sizeof(defaultInventories) / sizeof(defaultInventories[0]));

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

int RpgBlockInventory_Count(void) { return inventoryCount; }
const RpgBlockInventory *RpgBlockInventory_Get(int index)
{
    return index >= 0 && index < RpgBlockInventory_Count() ? &inventories[index] : &inventories[0];
}

RpgBlockInventory *RpgBlockInventory_GetMutable(int index)
{
    return index >= 0 && index < RpgBlockInventory_Count() ? &inventories[index] : NULL;
}

bool RpgBlockInventory_SetName(int index, const char *name)
{
    RpgBlockInventory *inventory = RpgBlockInventory_GetMutable(index);
    if (inventory == NULL || name == NULL || name[0] == '\0') return false;
    snprintf(inventory->name, sizeof(inventory->name), "%s", name);
    return true;
}

bool RpgBlockInventory_SetBorderColor(int index, RpgBlockInventoryBorderColor color)
{
    RpgBlockInventory *inventory = RpgBlockInventory_GetMutable(index);
    if (inventory == NULL || color < RPG_BLOCK_INVENTORY_BORDER_WHITE ||
        color >= RPG_BLOCK_INVENTORY_BORDER_COLOR_COUNT) return false;
    inventory->borderColor = color;
    return true;
}

int RpgBlockInventory_Add(void)
{
    if (inventoryCount >= RPG_BLOCK_INVENTORY_MAX_PALETTES) return -1;
    RpgBlockInventory *inventory = &inventories[inventoryCount];
    memset(inventory, 0, sizeof(*inventory));
    snprintf(inventory->name, sizeof(inventory->name), "Palette %d", inventoryCount + 1);
    inventory->borderColor = RPG_BLOCK_INVENTORY_BORDER_WHITE;
    return inventoryCount++;
}

bool RpgBlockInventory_Remove(int index)
{
    if (index < 0 || index >= inventoryCount || inventoryCount <= 1) return false;
    if (index + 1 < inventoryCount)
        memmove(&inventories[index], &inventories[index + 1],
                (size_t)(inventoryCount - index - 1) * sizeof(inventories[0]));
    inventoryCount--;
    memset(&inventories[inventoryCount], 0, sizeof(inventories[0]));
    return true;
}

int RpgBlockInventory_MovePalette(int sourceIndex, int destinationIndex)
{
    if (sourceIndex < 0 || sourceIndex >= inventoryCount ||
        destinationIndex < 0 || destinationIndex >= inventoryCount) return -1;
    if (sourceIndex == destinationIndex) return sourceIndex;

    RpgBlockInventory moving = inventories[sourceIndex];
    if (sourceIndex < destinationIndex) {
        memmove(&inventories[sourceIndex], &inventories[sourceIndex + 1],
                (size_t)(destinationIndex - sourceIndex) * sizeof(inventories[0]));
    } else {
        memmove(&inventories[destinationIndex + 1], &inventories[destinationIndex],
                (size_t)(sourceIndex - destinationIndex) * sizeof(inventories[0]));
    }
    inventories[destinationIndex] = moving;
    return destinationIndex;
}

bool RpgBlockInventory_MoveBlock(int sourcePalette, int sourceSlot,
                                 int destinationPalette, int destinationSlot)
{
    if (sourcePalette < 0 || sourcePalette >= inventoryCount ||
        destinationPalette < 0 || destinationPalette >= inventoryCount) return false;
    RpgBlockInventory *source = &inventories[sourcePalette];
    RpgBlockInventory *destination = &inventories[destinationPalette];
    if (sourceSlot < 0 || sourceSlot >= source->count ||
        destinationSlot < 0 || destinationSlot > destination->count) return false;
    if (sourcePalette != destinationPalette && destination->count >= RPG_BLOCK_INVENTORY_MAX_SLOTS)
        return false;

    int blockType = source->blockTypes[sourceSlot];
    memmove(&source->blockTypes[sourceSlot], &source->blockTypes[sourceSlot + 1],
            (size_t)(source->count - sourceSlot - 1) * sizeof(source->blockTypes[0]));
    source->count--;
    if (sourcePalette == destinationPalette && destinationSlot > sourceSlot) destinationSlot--;
    if (destinationSlot < 0) destinationSlot = 0;
    if (destinationSlot > destination->count) destinationSlot = destination->count;
    memmove(&destination->blockTypes[destinationSlot + 1], &destination->blockTypes[destinationSlot],
            (size_t)(destination->count - destinationSlot) * sizeof(destination->blockTypes[0]));
    destination->blockTypes[destinationSlot] = blockType;
    destination->count++;
    return true;
}

static void SetPaletteBlocks(int index, const char *text)
{
    RpgBlockInventory *inventory = RpgBlockInventory_GetMutable(index);
    if (inventory == NULL || text == NULL) return;
    inventory->count = 0;
    while (*text != '\0' && inventory->count < RPG_BLOCK_INVENTORY_MAX_SLOTS) {
        char *end = NULL;
        long value = strtol(text, &end, 10);
        if (end == text) break;
        inventory->blockTypes[inventory->count++] = (int)value;
        text = *end == ',' ? end + 1 : end;
    }
}

bool RpgBlockInventory_LoadPreferences(const char *path)
{
    FILE *file;
    char line[256];
    if (path == NULL || path[0] == '\0' || (file = fopen(path, "rb")) == NULL) return false;
    while (fgets(line, (int)sizeof(line), file) != NULL) {
        int index = -1;
        int color = -1;
        int offset = 0;
        int count = 0;
        line[strcspn(line, "\r\n")] = '\0';
        if (sscanf(line, "palette_count=%d", &count) == 1) {
            count = count < 1 ? 1 : count;
            count = count > RPG_BLOCK_INVENTORY_MAX_PALETTES ? RPG_BLOCK_INVENTORY_MAX_PALETTES : count;
            while (inventoryCount < count) (void)RpgBlockInventory_Add();
            while (inventoryCount > count) (void)RpgBlockInventory_Remove(inventoryCount - 1);
        } else if (sscanf(line, "palette.%d.border=%d", &index, &color) == 2) {
            (void)RpgBlockInventory_SetBorderColor(index, (RpgBlockInventoryBorderColor)color);
        /* %n is not included in sscanf's conversion count.  A line such as
           "palette.0.blocks=..." assigns its numeric index before the
           literal ".name=" fails, so require a positive %n offset to prove
           the whole key prefix actually matched. */
        } else if (sscanf(line, "palette.%d.name=%n", &index, &offset) == 1 && offset > 0 &&
                   index >= 0 && index < RpgBlockInventory_Count() && line[offset] != '\0') {
            (void)RpgBlockInventory_SetName(index, line + offset);
        } else if (sscanf(line, "palette.%d.blocks=%n", &index, &offset) == 1 && offset > 0 &&
                   index >= 0 && index < RpgBlockInventory_Count()) {
            SetPaletteBlocks(index, line + offset);
        }
    }
    fclose(file);
    return true;
}

bool RpgBlockInventory_SavePreferences(const char *path)
{
    FILE *file;
    if (path == NULL || path[0] == '\0' || (file = fopen(path, "wb")) == NULL) return false;
    fprintf(file, "palette_count=%d\n", RpgBlockInventory_Count());
    for (int index = 0; index < RpgBlockInventory_Count(); index++) {
        const RpgBlockInventory *inventory = RpgBlockInventory_Get(index);
        fprintf(file, "palette.%d.name=%s\n", index, inventory->name);
        fprintf(file, "palette.%d.border=%d\n", index, (int)inventory->borderColor);
        fprintf(file, "palette.%d.blocks=", index);
        for (int blockIndex = 0; blockIndex < inventory->count; blockIndex++)
            fprintf(file, "%s%d", blockIndex == 0 ? "" : ",", inventory->blockTypes[blockIndex]);
        fputc('\n', file);
    }
    fclose(file);
    return true;
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
           blockType == RPG_BLOCK_ATTACHMENT_DATA_BUTTON || blockType == RPG_BLOCK_ATTACHMENT_SAVE_FLAG ||
           blockType == RPG_BLOCK_ATTACHMENT_BLOCK_SOCKET;
}

bool RpgBlockInventory_IsAttachmentBlock(int blockType)
{
    /* Normal attachments (button, flag, receiver and socket) belong to
       their supporting block only.  Only the emitter owns its outer cell as
       a real terrain block. */
    return blockType == RPG_BLOCK_ATTACHMENT_RADIO_EMITTER;
}

bool RpgBlockInventory_IsMapEventProperty(int blockType)
{
    return blockType == RPG_BLOCK_PROPERTY_MAP_EVENT;
}

bool RpgBlockInventory_IsConveyorProperty(int blockType)
{
    return blockType == RPG_BLOCK_PROPERTY_CONVEYOR;
}

bool RpgBlockInventory_IsOneWayPlatform(int blockType)
{
    return blockType == RPG_BLOCK_ONE_WAY_PLATFORM;
}

bool RpgBlockInventory_IsMagnetBlock(int blockType)
{
    return blockType == RPG_BLOCK_EFFECT_MAGNET_OFF || blockType == RPG_BLOCK_EFFECT_MAGNET_ON;
}

bool RpgBlockInventory_IsMagnetActive(int blockType)
{
    return blockType == RPG_BLOCK_EFFECT_MAGNET_ON;
}

bool RpgBlockInventory_IsMetalBlock(int blockType)
{
    return blockType == RPG_BLOCK_METAL;
}

bool RpgBlockInventory_IsPushBlock(int blockType)
{
    return blockType == RPG_BLOCK_PUSH_BLOCK;
}

bool RpgBlockInventory_IsSocketSignalBlock(int blockType)
{
    return blockType == RPG_BLOCK_SOCKET_SIGNAL_SOLID ||
           blockType == RPG_BLOCK_SOCKET_SIGNAL_ONE_WAY;
}

bool RpgBlockInventory_IsSocketSignalOneWayBlock(int blockType)
{
    return blockType == RPG_BLOCK_SOCKET_SIGNAL_ONE_WAY;
}

RpgBlockStructureKind RpgBlockInventory_GetStructureKind(int blockType)
{
    if ((blockType >= 1 && blockType <= 10) ||
        blockType == RPG_BLOCK_HOLE_VERTICAL || blockType == RPG_BLOCK_HOLE_HORIZONTAL ||
        RpgBlockInventory_IsOneWayPlatform(blockType) || RpgBlockInventory_IsMetalBlock(blockType) ||
        RpgBlockInventory_IsPushBlock(blockType) || RpgBlockInventory_IsSocketSignalBlock(blockType))
        return RPG_BLOCK_STRUCTURE_NORMAL_CELL;
    if (RpgBlockInventory_IsAttachmentBlock(blockType)) return RPG_BLOCK_STRUCTURE_NORMAL_CELL;
    if (RpgBlockInventory_IsAttachment(blockType)) return RPG_BLOCK_STRUCTURE_ATTACHMENT;
    if (RpgBlockInventory_IsReferenceObject(blockType) || blockType == RPG_BLOCK_IMAGE_OBJECT)
        return RPG_BLOCK_STRUCTURE_REFERENCE;
    if (blockType == RPG_BLOCK_PROPERTY_ITEM || blockType == RPG_BLOCK_PROPERTY_WIRE ||
        blockType == RPG_BLOCK_PROPERTY_RECEIVER || RpgBlockInventory_IsMapEventProperty(blockType) ||
        RpgBlockInventory_IsConveyorProperty(blockType)) return RPG_BLOCK_STRUCTURE_PROPERTY;
    {
        const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(blockType);
        if (shape != NULL)
            return shape->cellCount == 1 ? RPG_BLOCK_STRUCTURE_SPECIAL_CELL :
                                           RPG_BLOCK_STRUCTURE_COMPOSITE;
    }
    return RPG_BLOCK_STRUCTURE_NONE;
}

bool RpgBlockInventory_IsOrdinaryBlock(int blockType)
{
    return RpgBlockInventory_GetStructureKind(blockType) == RPG_BLOCK_STRUCTURE_NORMAL_CELL;
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
