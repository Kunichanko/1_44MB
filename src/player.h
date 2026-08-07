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
} Player;

Player Player_Create(Vector2 startPosition);
void Player_Update(Player *player, float deltaTime, float worldWidth);
void Player_Draw(const Player *player, float groundY);
bool Player_SetAppearance(Player *player, const char *filePath);
void Player_UnloadAppearance(Player *player);
Rectangle Player_GetBounds(const Player *player, float groundY);

#endif
