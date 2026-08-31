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
    RPG_BLOCK_REFERENCE_FOLDER = 36,
    /* 見た目専用PNGを独立オブジェクトとして配置するためのパレット項目。 */
    RPG_BLOCK_IMAGE_OBJECT = 37,
    /* 下・横からは通過し、上から落下したプレイヤーだけを受け止める1マス床。 */
    RPG_BLOCK_ONE_WAY_PLATFORM = 38,
    /* 受容体から届いた電気で状態を切り替える1マスの磁石。 */
    RPG_BLOCK_EFFECT_MAGNET_OFF = 39,
    RPG_BLOCK_EFFECT_MAGNET_ON = 40,
    /* 重力と磁石の吸引対象になる、通常ブロック扱いの金属マス。 */
    RPG_BLOCK_METAL = 41,
    /* 指定Fileを格納した時だけ開く、通常Doorとは別意匠の3マス扉。 */
    RPG_BLOCK_KEY_DOOR_CLOSED_TOP = 42,
    RPG_BLOCK_KEY_DOOR_CLOSED_MIDDLE = 43,
    RPG_BLOCK_KEY_DOOR_CLOSED_BOTTOM = 44,
    RPG_BLOCK_KEY_DOOR_OPEN_TOP = 45,
    RPG_BLOCK_KEY_DOOR_OPEN_MIDDLE = 46,
    RPG_BLOCK_KEY_DOOR_OPEN_BOTTOM = 47,
    /* Player can hold this gravity block with G; it is not magnetizable. */
    RPG_BLOCK_PUSH_BLOCK = 48,
    RPG_BLOCK_ATTACHMENT_RADIO_EMITTER = 200,
    RPG_BLOCK_ATTACHMENT_DATA_BUTTON = 201,
    RPG_BLOCK_ATTACHMENT_SAVE_FLAG = 202,
    RPG_BLOCK_PROPERTY_ITEM = 100,
    RPG_BLOCK_PROPERTY_WIRE = 101,
    RPG_BLOCK_PROPERTY_RECEIVER = 102,
    /* 通常イベントはブロックモードのパレットから、マスに沿って配置する。 */
    RPG_BLOCK_PROPERTY_MAP_EVENT = 103,
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
bool RpgBlockInventory_IsReferenceFolder(int blockType);
bool RpgBlockInventory_IsDoorBlock(int blockType);
bool RpgBlockInventory_IsDoorOpen(int blockType);
bool RpgBlockInventory_IsKeyDoorBlock(int blockType);
bool RpgBlockInventory_IsKeyDoorOpen(int blockType);
bool RpgBlockInventory_IsSignalShrinkBlock(int blockType);
bool RpgBlockInventory_IsAttachment(int blockType);
bool RpgBlockInventory_IsCellAttachment(int blockType);
bool RpgBlockInventory_IsMapEventProperty(int blockType);
bool RpgBlockInventory_IsOneWayPlatform(int blockType);
bool RpgBlockInventory_IsMagnetBlock(int blockType);
bool RpgBlockInventory_IsMagnetActive(int blockType);
bool RpgBlockInventory_IsMetalBlock(int blockType);
bool RpgBlockInventory_IsPushBlock(int blockType);
bool RpgBlockInventory_IsOrdinaryBlock(int blockType);
int RpgBlockInventory_GetEffectRootType(int blockType);
const RpgEffectShape *RpgBlockInventory_GetEffectShape(int blockType);
const RpgEffectShape *RpgBlockInventory_GetDoorShape(bool isOpen);
const RpgEffectShape *RpgBlockInventory_GetKeyDoorShape(bool isOpen);
#endif
// 役割: ブロック種別・パレット・特殊形状の定義を宣言する。
