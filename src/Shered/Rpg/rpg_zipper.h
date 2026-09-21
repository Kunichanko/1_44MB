// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_grid_path.h, rpg_inspect.h
#ifndef RPG_ZIPPER_H
#define RPG_ZIPPER_H

#include "rpg_character.h"
#include "rpg_grid_path.h"
#include "rpg_inspect.h"
#include "rpg_stage.h"

typedef enum RpgZipperHeldObjectKind {
    RPG_ZIPPER_HELD_OBJECT_NONE = 0,
    /* Only occupied grid blocks use the missing-cell/error-block route. */
    RPG_ZIPPER_HELD_OBJECT_BLOCK,
    /* Everything with its own runtime folder takes the movable route. */
    RPG_ZIPPER_HELD_OBJECT_MOVABLE
} RpgZipperHeldObjectKind;

typedef enum RpgZipperMovableKind {
    RPG_ZIPPER_MOVABLE_NONE = 0,
    RPG_ZIPPER_MOVABLE_DATA_SHOT,
    RPG_ZIPPER_MOVABLE_DYNAMIC_BLOCK,
    RPG_ZIPPER_MOVABLE_REFERENCE_FILE
} RpgZipperMovableKind;

/* ZipperがInboxへ保持しているフォルダ。現在くっついている対象とは独立して管理する。 */
typedef struct RpgZipperHeldObject {
    RpgZipperHeldObjectKind kind;
    RpgZipperMovableKind movableKind;
    RpgGridCell blockCell;
    int blockType;
    int dataShotIndex;
    int dynamicBlockIndex;
    /* Index is used only until eat removes the movable object.  The complete
       object record below is the durable FIFO return payload. */
    int referenceObjectIndex;
    RpgReferenceObject referenceObject;
} RpgZipperHeldObject;

enum { RPG_ZIPPER_HELD_QUEUE_CAPACITY = 31 };

typedef struct RpgZipper {
    RpgCharacter character;
    RpgInspect inspect;
    float launchSpeed;
    float returnSpeed;
    float followSpeed;
    bool launchPreviewEnabled;
    /* heldObject is the FIFO head.  Further captured objects wait here in
       capture order until spit returns them one at a time. */
    RpgZipperHeldObject heldObject;
    RpgZipperHeldObject heldQueue[RPG_ZIPPER_HELD_QUEUE_CAPACITY];
    int heldQueueCount;
    /* 返却演出中の旧所持物。次の所持物とは独立して、演出完了時にだけbuildへ確定する。 */
    RpgZipperHeldObject returningObject;
    /* 保存しない一時状態。取り込みアニメーション後に、所持フォルダを元位置へ返す。 */
    bool isFolderReturnPending;
    bool isSpitCommandPending;
    bool isFolderReturnAnimating;
    bool isFolderReturnCommitPending;
    float folderReturnDelayElapsed;
    float folderReturnElapsed;
    float folderReturnDuration;
    Vector2 folderReturnStart;
    Vector2 folderReturnDestination;
} RpgZipper;

RpgZipper RpgZipper_Default(void);
void RpgZipper_ClearHeldObject(RpgZipper *zipper);
bool RpgZipper_CanEnqueueHeldObject(const RpgZipper *zipper);
bool RpgZipper_EnqueueHeldObject(RpgZipper *zipper, RpgZipperHeldObject object);
void RpgZipper_RemoveNextHeldObject(RpgZipper *zipper);
/* Zipper-owned launch/return state transition.  The caller supplies a
 * world-space aim point only after its own modal/input conditions allow the
 * action. */
bool RpgZipper_TryToggleLaunch(RpgZipper *zipper, RpgCharacter *player,
                               Vector2 aimWorldPosition, bool *followsPlayer,
                               bool *isLaunched, Vector2 *launchVelocity);
bool RpgZipper_Load(const char *filePath, RpgZipper *zipper);
bool RpgZipper_Save(const char *filePath, const RpgZipper *zipper);
Rectangle RpgZipper_GetSpriteBounds(const RpgCharacter *character, float groundY);
Rectangle RpgZipper_GetPixelAlignedSpriteBounds(const RpgCharacter *character, float groundY);
void RpgZipper_DrawPointerFeedback(Rectangle bounds, bool isHovered, bool isSelected);

#endif
// 役割: Zipper の設定、境界、入力フィードバック API を宣言する。
