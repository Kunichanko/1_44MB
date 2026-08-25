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
    // 最後に実際に水平移動できた向き。1は右、-1は左。
    int facingDirection;
    Color shirtColor;
    Color hairColor;
} RpgCharacter;

RpgCharacter RpgCharacter_Create(Vector2 position, Color shirtColor, Color hairColor);
bool RpgCharacter_LoadPlayerSprites(void);
void RpgCharacter_UnloadPlayerSprites(void);
void RpgCharacter_UpdatePlayer(RpgCharacter *character, float deltaTime, float groundY,
                               float minimumX, float maximumX);
void RpgCharacter_UpdatePlayerWithStage(RpgCharacter *character, float deltaTime,
                                        const RpgStage *stage, float minimumX, float maximumX);
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
