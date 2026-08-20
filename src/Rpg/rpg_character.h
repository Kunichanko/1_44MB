// 依存する自プロジェクト内ファイル: rpg_stage.h
#ifndef RPG_CHARACTER_H
#define RPG_CHARACTER_H

#include "raylib.h"

#include "rpg_stage.h"

typedef struct RpgCharacter {
    Vector2 position;
    float verticalSpeed;
    bool isGrounded;
    float moveSpeed;
    float scale;
    Color shirtColor;
    Color hairColor;
} RpgCharacter;

RpgCharacter RpgCharacter_Create(Vector2 position, Color shirtColor, Color hairColor);
void RpgCharacter_UpdatePlayer(RpgCharacter *character, float deltaTime, float groundY,
                               float minimumX, float maximumX);
void RpgCharacter_UpdatePlayerWithStage(RpgCharacter *character, float deltaTime,
                                        const RpgStage *stage, float minimumX, float maximumX);
bool RpgCharacter_IsNear(const RpgCharacter *first, const RpgCharacter *second, float distance);
Rectangle RpgCharacter_GetFootBounds(const RpgCharacter *character);
Rectangle RpgCharacter_GetCollisionBounds(const RpgCharacter *character);
void RpgCharacter_Draw(const RpgCharacter *character, const char *name);

#endif
// 役割: RPG キャラクターの状態と操作 API を宣言する。
