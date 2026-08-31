// 依存する自プロジェクト内ファイル: rpg_stage.h
#ifndef RPG_CHARACTER_H
#define RPG_CHARACTER_H

#include "raylib.h"

#include "rpg_stage.h"

typedef enum RpgCharacterAnimation {
    RPG_CHARACTER_ANIMATION_AUTOMATIC,
    RPG_CHARACTER_ANIMATION_IDLE,
    RPG_CHARACTER_ANIMATION_WALK,
    RPG_CHARACTER_ANIMATION_JUMP,
    RPG_CHARACTER_ANIMATION_ZIPGO
} RpgCharacterAnimation;

typedef struct RpgCharacter {
    Vector2 position;
    float verticalSpeed;
    bool isGrounded;
    float moveSpeed;
    float scale;
    float animationElapsed;
    bool isMoving;
    /* EXE スプライトの不透明ピクセル範囲を物理判定に使う主人公だけ true。 */
    bool usesPlayerSpriteCollision;
    // 最後に実際に水平移動できた向き。1は右、-1は左。
    int facingDirection;
    Color shirtColor;
    Color hairColor;
} RpgCharacter;

/* 可動する足場・ブロックからプレイヤーへ渡す、種類に依存しない衝突情報。 */
typedef struct RpgMovingSolid {
    Rectangle previousBounds;
    Rectangle bounds;
} RpgMovingSolid;

typedef struct RpgMovingSolidSet {
    const RpgMovingSolid *entries;
    int count;
} RpgMovingSolidSet;

RpgCharacter RpgCharacter_Create(Vector2 position, Color shirtColor, Color hairColor);
void RpgCharacter_SetUsesPlayerSpriteCollision(RpgCharacter *character, bool enabled);
bool RpgCharacter_LoadPlayerSprites(void);
void RpgCharacter_UnloadPlayerSprites(void);
void RpgCharacter_UpdatePlayer(RpgCharacter *character, float deltaTime, float groundY,
                               float minimumX, float maximumX);
void RpgCharacter_UpdatePlayerWithStage(RpgCharacter *character, float deltaTime,
                                        const RpgStage *stage, float minimumX, float maximumX);
/* 静的ステージと可動する固体を共通に判定してプレイヤーを更新する。 */
void RpgCharacter_UpdatePlayerWithStageAndMovingSolids(RpgCharacter *character, float deltaTime,
                                                       const RpgStage *stage,
                                                       const RpgMovingSolidSet *movingSolids,
                                                       float minimumX, float maximumX);
/* Reuses the normal physics path while allowing runtime states to restrict player input. */
void RpgCharacter_UpdatePlayerWithStageAndMovingSolidsControlled(RpgCharacter *character, float deltaTime,
                                                                 const RpgStage *stage,
                                                                 const RpgMovingSolidSet *movingSolids,
                                                                 float minimumX, float maximumX,
                                                                 bool allowHorizontalInput, bool allowJumpInput);
/* 可動固体の移動後に、足場追従と軸限定の押し出しをプレイヤー側だけで解決する。 */
void RpgCharacter_ResolveMovingSolidContacts(RpgCharacter *character, const RpgStage *stage,
                                             const RpgMovingSolidSet *movingSolids);
bool RpgCharacter_IsNear(const RpgCharacter *first, const RpgCharacter *second, float distance);
Rectangle RpgCharacter_GetFootBounds(const RpgCharacter *character);
Rectangle RpgCharacter_GetCollisionBounds(const RpgCharacter *character);
/* 描画済みスプライトと名前ラベルを含む範囲。物理用のCollisionBoundsとは分け、エディターの選択判定に使う。 */
Rectangle RpgCharacter_GetVisualBounds(const RpgCharacter *character);
void RpgCharacter_ResetAnimation(RpgCharacter *character);
void RpgCharacter_DrawPlayer(const RpgCharacter *character, RpgCharacterAnimation animation);
void RpgCharacter_DrawPlayerTinted(const RpgCharacter *character, RpgCharacterAnimation animation, Color tint);
void RpgCharacter_Draw(const RpgCharacter *character, const char *name);
void RpgCharacter_DrawTinted(const RpgCharacter *character, const char *name, Color tint);

#endif
// 役割: RPG キャラクターの状態と操作 API を宣言する。
