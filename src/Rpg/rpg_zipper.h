// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_grid_path.h, rpg_inspect.h
#ifndef RPG_ZIPPER_H
#define RPG_ZIPPER_H

#include "rpg_character.h"
#include "rpg_grid_path.h"
#include "rpg_inspect.h"

typedef enum RpgZipperHeldObjectKind {
    RPG_ZIPPER_HELD_OBJECT_NONE = 0,
    RPG_ZIPPER_HELD_OBJECT_BLOCK,
    RPG_ZIPPER_HELD_OBJECT_ATTACHMENT,
    RPG_ZIPPER_HELD_OBJECT_DATA_SHOT
} RpgZipperHeldObjectKind;

/* ZipperがInboxへ保持しているフォルダ。現在くっついている対象とは独立して管理する。 */
typedef struct RpgZipperHeldObject {
    RpgZipperHeldObjectKind kind;
    RpgGridCell blockCell;
    int blockType;
    int attachmentIndex;
    int dataShotIndex;
} RpgZipperHeldObject;

typedef struct RpgZipper {
    RpgCharacter character;
    RpgInspect inspect;
    float launchSpeed;
    float returnSpeed;
    float followSpeed;
    bool launchPreviewEnabled;
    RpgZipperHeldObject heldObject;
    /* 返却演出中の旧所持物。次の所持物とは独立して、演出完了時にだけbuildへ確定する。 */
    RpgZipperHeldObject returningObject;
    /* 保存しない一時状態。取り込みアニメーション後に、所持フォルダを元位置へ返す。 */
    bool isFolderReturnPending;
    bool isFolderReturnAnimating;
    bool isFolderReturnCommitPending;
    float folderReturnElapsed;
    float folderReturnDuration;
    Vector2 folderReturnStart;
    Vector2 folderReturnDestination;
} RpgZipper;

RpgZipper RpgZipper_Default(void);
void RpgZipper_ClearHeldObject(RpgZipper *zipper);
bool RpgZipper_Load(const char *filePath, RpgZipper *zipper);
bool RpgZipper_Save(const char *filePath, const RpgZipper *zipper);
Rectangle RpgZipper_GetSpriteBounds(const RpgCharacter *character, float groundY);
void RpgZipper_DrawPointerFeedback(Rectangle bounds, bool isHovered, bool isSelected);

#endif
// 役割: Zipper の設定、境界、入力フィードバック API を宣言する。
