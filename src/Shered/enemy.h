// 依存: なし（raylib は外部ライブラリ）
#ifndef ENEMY_H
#define ENEMY_H

#include "raylib.h"

#include <stdbool.h>

typedef struct Enemy {
    Vector2 position;
    float width;
    float height;
    float moveSpeed;
    float patrolLeft;
    float patrolRight;
    int moveDirection;
    Texture2D appearance;
    bool hasAppearance;
    bool isActive;
    bool isSubordinate;
    int followOrder;
    float followTargetX;
    float followSpacing;
    float followInterpolationSpeed;
    Color subordinateColor;
} Enemy;

Enemy Enemy_Create(float startX, float groundY, float patrolLeft, float patrolRight);
bool Enemy_LoadAppearance(Enemy *enemy, const char *filePath);
bool Enemy_TryBecomeSubordinate(Enemy *enemy, Vector2 leaderPosition, int leaderDirection,
                                Vector2 actionOriginPosition, float leaderScale,
                                float actionRange, int followOrder);
void Enemy_Update(Enemy *enemy, float deltaTime, Vector2 leaderPosition, int leaderDirection,
                  float leaderScale, float maximumMoveSpeed);
bool Enemy_IsSubordinate(const Enemy *enemy);
void Enemy_SetFollowSettings(Enemy *enemy, float spacing, float interpolationSpeed,
                             Color subordinateColor);
void Enemy_SetScale(Enemy *enemy, float scale);
float Enemy_GetScale(const Enemy *enemy);
Rectangle Enemy_GetBounds(const Enemy *enemy, float groundY);
void Enemy_Draw(const Enemy *enemy, float groundY);
void Enemy_UnloadAppearance(Enemy *enemy);

#endif
// 役割: 横スクロール敵一体の状態と操作 API を宣言する。
