// 役割: 磁石・金属ブロックの電気切替、吸引範囲描画、重力と吸引によるマス移動を管理する。
// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_stage.h
#ifndef RPG_MAGNET_H
#define RPG_MAGNET_H

#include "rpg_attachment.h"
#include "rpg_button_event.h"
#include "rpg_character.h"
#include "rpg_wire.h"

enum { RPG_MAGNET_MAX_METALS = RPG_STAGE_ROWS * RPG_STAGE_WORLD_COLUMNS };
enum { RPG_PUSH_BLOCK_INTERACTION_RANGE = 72 };
/* G can reach horizontally within its normal range, but only across half a
   tile of vertical separation. */
enum { RPG_PUSH_BLOCK_INTERACTION_VERTICAL_RANGE = RPG_STAGE_TILE_SIZE / 2 };

typedef struct RpgMagnetMetal {
    Vector2 position;
    Vector2 previousPosition;
    float verticalSpeed;
    bool isGrounded;
    /* -1 while free; otherwise this block is fixed in the matching socket. */
    int lockedSocketAttachmentIndex;
    /* Downward distance already applied to position while seating.  The
       supporting tile removes this exact top slice from collision, so visual
       and physical bounds stay aligned inside the socket recess. */
    float socketSettleOffset;
    /* G can release a fixed push block without moving it.  Keep it free until
       it has actually left the socket, otherwise the next world update would
       immediately re-lock it before the player can begin carrying it. */
    bool requiresSocketDeparture;
    /* Stable identity for the object's folder.  Its physical position is
       allowed to move independently of this original cell. */
    RpgGridCell objectCell;
    int blockType;
    bool active;
} RpgMagnetMetal;

/* Player-owned state for holding a push block. The dynamic block system remains generic. */
typedef struct RpgPlayerPushState {
    int heldBlockIndex;
    /* Horizontal distance from the player's collision center to the held
       block's center.  Carrying uses exactly one tile in this direction. */
    float playerToBlockCenterOffsetX;
    /* The first world tick may need to resolve a pre-existing wall contact.
       Thereafter the interaction range, rather than a sub-pixel alignment
       error, decides when carrying is released. */
    bool carryDistanceAligned;
} RpgPlayerPushState;

typedef struct RpgMagnetRuntime {
    bool isInitialized;
    /* Magnets consume the shared area communication independently from the
       electrical data-shot/receiver system. */
    unsigned int lastCommunicationSequence;
    int metalCount;
    RpgMagnetMetal metals[RPG_MAGNET_MAX_METALS];
    RpgMovingSolid movingSolids[RPG_MAGNET_MAX_METALS];
} RpgMagnetRuntime;

RpgMagnetRuntime RpgMagnetRuntime_Default(void);
RpgPlayerPushState RpgPlayerPushState_Default(void);
void RpgMagnets_ConsumeCommunication(RpgMagnetRuntime *runtime, RpgStage *stage,
                                     const RpgButtonEvent *communication);
/* ステージに保存された金属を、実行時だけ細かく動く固体へ変換する。 */
void RpgMagnets_InitializeForStage(RpgMagnetRuntime *runtime, RpgStage *stage);
/* Starts a world frame; movement code may replace previousPosition before changing a block. */
void RpgMagnets_BeginFrame(RpgMagnetRuntime *runtime);
/* Locks a fully seated runtime block into a top-mounted block socket. */
void RpgMagnets_LockInBlockSockets(RpgMagnetRuntime *runtime, RpgStage *stage,
                                   const RpgAttachments *attachments, RpgButtonEvent *event,
                                   RpgPlayerPushState *pushState);
/* Socket signal state is occupancy-driven, not lock-driven: an area stays
   active whenever any runtime block fully covers one of its socket beams. */
void RpgMagnets_UpdateBlockSocketSignals(const RpgMagnetRuntime *runtime, RpgStage *stage,
                                         const RpgAttachments *attachments,
                                         RpgButtonEvent *event);
void RpgMagnets_Update(RpgMagnetRuntime *runtime, RpgStage *stage, const RpgWires *wires,
                       float pixelsPerSecond, float deltaTime,
                       const RpgPlayerPushState *pushState);
void RpgMagnets_ApplyConveyors(RpgMagnetRuntime *runtime, const RpgStage *stage,
                               const RpgWires *wires, float deltaTime,
                               const RpgPlayerPushState *pushState);
RpgMovingSolidSet RpgMagnets_GetMovingSolids(const RpgMagnetRuntime *runtime);
RpgMovingSolidSet RpgMagnets_GetMovingSolidsExcept(const RpgMagnetRuntime *runtime, int excludedIndex,
                                                    RpgMovingSolid *storage, int storageCapacity);
bool RpgMagnets_TogglePlayerPush(RpgMagnetRuntime *runtime, RpgStage *stage,
                                 RpgPlayerPushState *pushState, RpgCharacter *player,
                                 float maximumDistance, RpgButtonEvent *event);
bool RpgMagnets_IsPlayerPushHeld(const RpgMagnetRuntime *runtime,
                                 const RpgPlayerPushState *pushState);
/* Keeps the carried block and player one tile apart without allowing either
   collider to overlap.  Carrying is released only after their distance
   exceeds RPG_PUSH_BLOCK_INTERACTION_RANGE, matching the G interaction. */
bool RpgMagnets_MoveHeldPushBlock(RpgMagnetRuntime *runtime, const RpgStage *stage,
                                  RpgPlayerPushState *pushState, RpgCharacter *player,
                                  float playerMovementX);
/* Area transitions remap the player between packed storage slots.  Keep a
   held push block in that same coordinate system without creating a false
   moving-solid displacement on the transition frame. */
void RpgMagnets_TranslateHeldPushBlock(RpgMagnetRuntime *runtime,
                                       const RpgPlayerPushState *pushState, Vector2 delta);
void RpgMagnets_DrawMetals(const RpgMagnetRuntime *runtime, int firstColumn, int columnCount,
                           float worldOffsetX, float brightness);
void RpgMagnets_DrawFields(const RpgStage *stage, int firstColumn, int columnCount);
int RpgMagnets_FindBlockHit(const RpgMagnetRuntime *runtime, Rectangle bounds);
bool RpgMagnets_SetBlockZipperHeld(RpgMagnetRuntime *runtime, int index, bool isHeld);

#endif
