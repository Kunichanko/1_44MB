// 依存: なし（raylib は外部ライブラリ）
#ifndef PLAYER_H
#define PLAYER_H

#include "raylib.h"

#include <stdbool.h>

typedef struct Player {
    Vector2 position;
    float width;
    float height;
    float moveSpeed;
    Texture2D appearance;
    bool hasAppearance;
    Texture2D defaultAppearance;
    bool hasDefaultAppearance;
    int animationFrame;
    float animationElapsed;
    bool actionActive;
    bool actionStarted;
    int facingDirection;
} Player;

Player Player_Create(Vector2 startPosition);
bool Player_LoadDefaultAppearance(Player *player, const char *filePath);
void Player_Update(Player *player, float deltaTime, float worldWidth);
bool Player_ConsumeActionStarted(Player *player);
bool Player_IsActionActive(const Player *player);
int Player_GetFacingDirection(const Player *player);
float Player_GetMoveSpeed(const Player *player);
void Player_Draw(const Player *player, float groundY);
bool Player_SetAppearance(Player *player, const char *filePath);
void Player_UnloadAppearance(Player *player);
void Player_UnloadDefaultAppearance(Player *player);
void Player_SetScale(Player *player, float scale);
float Player_GetScale(const Player *player);
Rectangle Player_GetBounds(const Player *player, float groundY);

#endif
