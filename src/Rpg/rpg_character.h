// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_CHARACTER_H
#define RPG_CHARACTER_H

#include "raylib.h"

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
bool RpgCharacter_IsNear(const RpgCharacter *first, const RpgCharacter *second, float distance);
Rectangle RpgCharacter_GetFootBounds(const RpgCharacter *character);
void RpgCharacter_Draw(const RpgCharacter *character, const char *name);

#endif
