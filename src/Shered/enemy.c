// 依存: enemy.h
#include "enemy.h"

static const float ENEMY_BASE_WIDTH = 48.0f;
static const float ENEMY_BASE_HEIGHT = 60.0f;

static float InterpolatePosition(float current, float target, float deltaTime,
                                 float interpolationSpeed, float maximumMoveSpeed)
{
    float interpolation = interpolationSpeed * deltaTime;
    if (interpolation > 1.0f) {
        interpolation = 1.0f;
    }

    float interpolatedPosition = current + (target - current) * interpolation;
    float movement = interpolatedPosition - current;
    float maximumMovement = maximumMoveSpeed * deltaTime;
    if (movement > maximumMovement) {
        return current + maximumMovement;
    }
    if (movement < -maximumMovement) {
        return current - maximumMovement;
    }
    return interpolatedPosition;
}

static float GetFollowDistance(const Enemy *enemy, float leaderScale)
{
    if (enemy->followOrder == 1) {
        return enemy->followSpacing * leaderScale;
    }
    return enemy->followSpacing;
}

Enemy Enemy_Create(float startX, float groundY, float patrolLeft, float patrolRight)
{
    return (Enemy){
        .position = { startX, groundY },
        .width = ENEMY_BASE_WIDTH * 1.5f,
        .height = ENEMY_BASE_HEIGHT * 1.5f,
        .moveSpeed = 100.0f,
        .patrolLeft = patrolLeft,
        .patrolRight = patrolRight,
        .moveDirection = 1,
        .appearance = {0},
        .hasAppearance = false,
        .isActive = true,
        .isSubordinate = false,
        .followOrder = 0,
        .followTargetX = startX,
        .followSpacing = 20.0f,
        .followInterpolationSpeed = 6.0f,
        .subordinateColor = LIME,
    };
}

bool Enemy_LoadAppearance(Enemy *enemy, const char *filePath)
{
    Texture2D appearance = LoadTexture(filePath);
    if (!IsTextureValid(appearance)) {
        return false;
    }

    Enemy_UnloadAppearance(enemy);
    enemy->appearance = appearance;
    enemy->hasAppearance = true;
    return true;
}

bool Enemy_TryBecomeSubordinate(Enemy *enemy, Vector2 leaderPosition, int leaderDirection,
                                Vector2 actionOriginPosition, float leaderScale,
                                float actionRange, int followOrder)
{
    if (!enemy->isActive) {
        return false;
    }
    float distanceX = enemy->position.x - actionOriginPosition.x;
    if (distanceX < 0.0f) {
        distanceX = -distanceX;
    }

    if (enemy->isSubordinate || distanceX > actionRange) {
        return false;
    }

    enemy->isSubordinate = true;
    enemy->followOrder = followOrder;
    enemy->followTargetX = leaderPosition.x -
                           GetFollowDistance(enemy, leaderScale) * leaderDirection;
    return true;
}

void Enemy_Update(Enemy *enemy, float deltaTime, Vector2 leaderPosition, int leaderDirection,
                  float leaderScale, float maximumMoveSpeed)
{
    if (!enemy->isActive) {
        return;
    }
    if (enemy->isSubordinate) {
        // 従属順に応じた間隔で目標位置を更新し、後ろの敵ほど少し遅れて追従させる。
        enemy->followTargetX = leaderPosition.x -
                               GetFollowDistance(enemy, leaderScale) * leaderDirection;

        // 従属順に応じた更新間隔で目標位置を更新し、追従に自然な遅延を与える。

        // 目標位置との距離に応じた補間速度で近づけ、急な位置変更を避けて滑らかに追従する。
        enemy->position.x = InterpolatePosition(enemy->position.x, enemy->followTargetX, deltaTime,
                                                 enemy->followInterpolationSpeed, maximumMoveSpeed);
        return;
    }

    enemy->position.x += enemy->moveDirection * enemy->moveSpeed * deltaTime;

    // 巡回範囲の端に着いたら位置を補正してから向きを反転し、範囲外へ出ないようにする。
    if (enemy->position.x >= enemy->patrolRight) {
        enemy->position.x = enemy->patrolRight;
        enemy->moveDirection = -1;
    } else if (enemy->position.x <= enemy->patrolLeft) {
        enemy->position.x = enemy->patrolLeft;
        enemy->moveDirection = 1;
    }
}

bool Enemy_IsSubordinate(const Enemy *enemy)
{
    return enemy->isSubordinate;
}

void Enemy_SetFollowSettings(Enemy *enemy, float spacing, float interpolationSpeed,
                             Color subordinateColor)
{
    enemy->followSpacing = spacing;
    enemy->followInterpolationSpeed = interpolationSpeed;
    enemy->subordinateColor = subordinateColor;
}

void Enemy_SetScale(Enemy *enemy, float scale)
{
    enemy->width = ENEMY_BASE_WIDTH * scale;
    enemy->height = ENEMY_BASE_HEIGHT * scale;
}

float Enemy_GetScale(const Enemy *enemy)
{
    return enemy->width / ENEMY_BASE_WIDTH;
}

Rectangle Enemy_GetBounds(const Enemy *enemy, float groundY)
{
    return (Rectangle){
        enemy->position.x - enemy->width / 2.0f,
        groundY - enemy->height,
        enemy->width,
        enemy->height,
    };
}

void Enemy_Draw(const Enemy *enemy, float groundY)
{
    if (!enemy->isActive) {
        return;
    }
    Rectangle destination = Enemy_GetBounds(enemy, groundY);

    if (enemy->hasAppearance) {
        Rectangle source = { 0.0f, 0.0f, (float)enemy->appearance.width,
                             (float)enemy->appearance.height };
        DrawTexturePro(enemy->appearance, source, destination, (Vector2){0}, 0.0f,
                       enemy->isSubordinate ? enemy->subordinateColor : WHITE);
        return;
    }

    DrawRectangleRec(destination, enemy->isSubordinate ? enemy->subordinateColor : RED);
}

void Enemy_UnloadAppearance(Enemy *enemy)
{
    if (enemy->hasAppearance) {
        UnloadTexture(enemy->appearance);
        enemy->appearance = (Texture2D){0};
        enemy->hasAppearance = false;
    }
}
