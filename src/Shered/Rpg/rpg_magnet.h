// 役割: 磁石・金属ブロックの電気切替、吸引範囲描画、重力と吸引によるマス移動を管理する。
// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_stage.h
#ifndef RPG_MAGNET_H
#define RPG_MAGNET_H

#include "rpg_character.h"

enum { RPG_MAGNET_MAX_METALS = RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS };

typedef struct RpgMagnetMetal {
    Vector2 position;
    Vector2 previousPosition;
    int blockType;
    bool active;
} RpgMagnetMetal;

/* Player-owned state for holding a push block. The dynamic block system remains generic. */
typedef struct RpgPlayerPushState {
    int heldBlockIndex;
    float playerToBlockOffsetX;
} RpgPlayerPushState;

typedef struct RpgMagnetRuntime {
    bool isInitialized;
    int metalCount;
    RpgMagnetMetal metals[RPG_MAGNET_MAX_METALS];
    RpgMovingSolid movingSolids[RPG_MAGNET_MAX_METALS];
} RpgMagnetRuntime;

RpgMagnetRuntime RpgMagnetRuntime_Default(void);
RpgPlayerPushState RpgPlayerPushState_Default(void);
bool RpgMagnets_ToggleAtCell(RpgStage *stage, int row, int column);
/* ステージに保存された金属を、実行時だけ細かく動く固体へ変換する。 */
void RpgMagnets_InitializeForStage(RpgMagnetRuntime *runtime, RpgStage *stage);
/* Starts a world frame; movement code may replace previousPosition before changing a block. */
void RpgMagnets_BeginFrame(RpgMagnetRuntime *runtime);
void RpgMagnets_Update(RpgMagnetRuntime *runtime, RpgStage *stage, float pixelsPerSecond, float deltaTime,
                       const RpgPlayerPushState *pushState);
RpgMovingSolidSet RpgMagnets_GetMovingSolids(const RpgMagnetRuntime *runtime);
RpgMovingSolidSet RpgMagnets_GetMovingSolidsExcept(const RpgMagnetRuntime *runtime, int excludedIndex,
                                                    RpgMovingSolid *storage, int storageCapacity);
bool RpgMagnets_TogglePlayerPush(RpgMagnetRuntime *runtime, RpgStage *stage,
                                 RpgPlayerPushState *pushState, Vector2 playerPosition,
                                 float maximumDistance);
bool RpgMagnets_IsPlayerPushHeld(const RpgMagnetRuntime *runtime,
                                 const RpgPlayerPushState *pushState);
float RpgMagnets_GetHeldPushBlockX(const RpgMagnetRuntime *runtime,
                                   const RpgPlayerPushState *pushState);
float RpgMagnets_MoveHeldPushBlock(RpgMagnetRuntime *runtime, const RpgStage *stage,
                                   const RpgPlayerPushState *pushState, float amountX);
/* Area transitions remap the player between packed storage slots.  Keep a
   held push block in that same coordinate system without creating a false
   moving-solid displacement on the transition frame. */
void RpgMagnets_TranslateHeldPushBlock(RpgMagnetRuntime *runtime,
                                       const RpgPlayerPushState *pushState, Vector2 delta);
void RpgMagnets_DrawMetals(const RpgMagnetRuntime *runtime, int firstColumn, int columnCount,
                           float worldOffsetX, float brightness);
void RpgMagnets_DrawFields(const RpgStage *stage, int firstColumn, int columnCount);

#endif
