// 依存: enemy_group.h、enemy.h
#include "enemy_group.h"

#include <stddef.h>

static float GetHorizontalDistance(float firstX, float secondX)
{
    float distance = firstX - secondX;
    return distance < 0.0f ? -distance : distance;
}

static Enemy *FindEnemyByFollowOrder(EnemyGroup *group, int followOrder)
{
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        if (group->enemies[index].isSubordinate &&
            group->enemies[index].followOrder == followOrder) {
            return &group->enemies[index];
        }
    }
    return NULL;
}

static void RecordLeaderPosition(EnemyGroup *group, Vector2 leaderPosition, float deltaTime)
{
    for (int index = 0; index < group->leaderPositionHistoryCount; index++) {
        group->leaderPositionAge[index] += deltaTime;
    }

    int newestIndex = group->leaderPositionHistoryCount;
    if (newestIndex >= ENEMY_GROUP_POSITION_HISTORY_COUNT) {
        newestIndex = ENEMY_GROUP_POSITION_HISTORY_COUNT - 1;
    }
    for (int index = newestIndex; index > 0; index--) {
        group->leaderPositionHistory[index] = group->leaderPositionHistory[index - 1];
        group->leaderPositionAge[index] = group->leaderPositionAge[index - 1];
    }

    group->leaderPositionHistory[0] = leaderPosition;
    group->leaderPositionAge[0] = 0.0f;
    if (group->leaderPositionHistoryCount < ENEMY_GROUP_POSITION_HISTORY_COUNT) {
        group->leaderPositionHistoryCount++;
    }
}

static Vector2 GetDelayedLeaderPosition(const EnemyGroup *group, Vector2 currentPosition,
                                        float delaySeconds)
{
    if (group->leaderPositionHistoryCount == 0 || delaySeconds <= 0.0f) {
        return currentPosition;
    }

    for (int index = 1; index < group->leaderPositionHistoryCount; index++) {
        float olderAge = group->leaderPositionAge[index];
        if (olderAge >= delaySeconds) {
            float newerAge = group->leaderPositionAge[index - 1];
            float ageRange = olderAge - newerAge;
            float ratio = ageRange > 0.0f ? (delaySeconds - newerAge) / ageRange : 0.0f;
            Vector2 newerPosition = group->leaderPositionHistory[index - 1];
            Vector2 olderPosition = group->leaderPositionHistory[index];
            return (Vector2){
                newerPosition.x + (olderPosition.x - newerPosition.x) * ratio,
                newerPosition.y + (olderPosition.y - newerPosition.y) * ratio,
            };
        }
    }

    return group->leaderPositionHistory[group->leaderPositionHistoryCount - 1];
}

static Vector2 GetPlannedLeaderPosition(const EnemyGroup *group, Vector2 playerPosition,
                                        int leaderDirection, float leaderScale, int followOrder)
{
    Vector2 plannedPosition = GetDelayedLeaderPosition(group, playerPosition,
                                                        (float)followOrder * 0.05f);
    for (int order = 1; order < followOrder; order++) {
        float spacing = order == 1 ? group->followSpacing * leaderScale : group->followSpacing;
        plannedPosition.x -= spacing * leaderDirection;
    }
    return plannedPosition;
}

EnemyGroup EnemyGroup_Create(float groundY)
{
    return (EnemyGroup){
        .enemies = {
            Enemy_Create(720.0f, groundY, 528.0f, 816.0f),
            Enemy_Create(1104.0f, groundY, 912.0f, 1200.0f),
            Enemy_Create(1488.0f, groundY, 1296.0f, 1584.0f),
        },
        .followSpacing = 20.0f,
        .followInterpolationSpeed = 6.0f,
        .subordinateColor = LIME,
    };
}

void EnemyGroup_LoadAppearance(EnemyGroup *group, const char *filePath)
{
    // 全ての敵が同一PNGを使うため、GPUテクスチャは一度だけ読み込み、各敵で共有する。
    if (!Enemy_LoadAppearance(&group->enemies[0], filePath)) {
        return;
    }
    for (int index = 1; index < ENEMY_GROUP_MAX_COUNT; index++) {
        group->enemies[index].appearance = group->enemies[0].appearance;
        group->enemies[index].hasAppearance = true;
    }
}

bool EnemyGroup_TrySubordinateNearest(EnemyGroup *group, Vector2 leaderPosition,
                                      int leaderDirection, float leaderScale, float actionRange)
{
    int nearestIndex = -1;
    float nearestDistance = actionRange;

    // 未従属の敵だけを比較し、アクション範囲内で最も近い一体を従属化する。
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        Enemy *enemy = &group->enemies[index];
        float distance = GetHorizontalDistance(enemy->position.x, leaderPosition.x);
        if (enemy->isActive && !enemy->isSubordinate && distance <= nearestDistance) {
            nearestIndex = index;
            nearestDistance = distance;
        }
    }

    if (nearestIndex < 0) {
        return false;
    }

    int followOrder = EnemyGroup_GetSubordinateCount(group) + 1;
    Vector2 targetLeaderPosition = GetPlannedLeaderPosition(group, leaderPosition,
                                                            leaderDirection, leaderScale,
                                                            followOrder);
    return Enemy_TryBecomeSubordinate(&group->enemies[nearestIndex], targetLeaderPosition,
                                      leaderDirection, leaderPosition, leaderScale,
                                      actionRange, followOrder);
}

void EnemyGroup_SetSpawnPositions(EnemyGroup *group, const Vector2 *positions, int count,
                                  float patrolHalfWidth)
{
    if (count < 0) {
        count = 0;
    } else if (count > ENEMY_GROUP_MAX_COUNT) {
        count = ENEMY_GROUP_MAX_COUNT;
    }

    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        Enemy *enemy = &group->enemies[index];
        enemy->isActive = index < count;
        enemy->isSubordinate = false;
        enemy->followOrder = 0;
        if (index < count) {
            enemy->position = positions[index];
            enemy->patrolLeft = positions[index].x - patrolHalfWidth;
            enemy->patrolRight = positions[index].x + patrolHalfWidth;
            enemy->moveDirection = 1;
            enemy->followTargetX = positions[index].x;
        }
    }
}

void EnemyGroup_Update(EnemyGroup *group, float deltaTime, Vector2 leaderPosition,
                       int leaderDirection, float leaderScale, float leaderMoveSpeed)
{
    RecordLeaderPosition(group, leaderPosition, deltaTime);
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        if (!Enemy_IsSubordinate(&group->enemies[index])) {
            Enemy_Update(&group->enemies[index], deltaTime, leaderPosition, leaderDirection,
                         leaderScale, leaderMoveSpeed);
        }
    }

    // 遅延更新の参照元をプレイヤー基準で事前計算し、後続の敵にずれを連鎖させない。
    for (int followOrder = 1; followOrder <= ENEMY_GROUP_MAX_COUNT; followOrder++) {
        Enemy *enemy = FindEnemyByFollowOrder(group, followOrder);
        if (enemy == NULL) {
            continue;
        }

        Vector2 targetLeaderPosition = GetPlannedLeaderPosition(group, leaderPosition,
                                                                leaderDirection, leaderScale,
                                                                followOrder);
        Enemy_Update(enemy, deltaTime, targetLeaderPosition, leaderDirection, leaderScale,
                     leaderMoveSpeed);
    }
}

void EnemyGroup_Draw(const EnemyGroup *group, float groundY)
{
    // 非従属の敵を先に描き、従属順が後ろの敵から前の敵へ重ねて描画する。
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        if (!Enemy_IsSubordinate(&group->enemies[index])) {
            Enemy_Draw(&group->enemies[index], groundY);
        }
    }

    for (int order = ENEMY_GROUP_MAX_COUNT; order > 0; order--) {
        for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
            const Enemy *enemy = &group->enemies[index];
            if (enemy->isSubordinate && enemy->followOrder == order) {
                Enemy_Draw(enemy, groundY);
            }
        }
    }
}

int EnemyGroup_GetSubordinateCount(const EnemyGroup *group)
{
    int count = 0;
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        if (Enemy_IsSubordinate(&group->enemies[index])) {
            count++;
        }
    }

    return count;
}

int EnemyGroup_GetActiveCount(const EnemyGroup *group)
{
    int count = 0;
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        if (group->enemies[index].isActive) {
            count++;
        }
    }
    return count;
}

void EnemyGroup_SetFollowSettings(EnemyGroup *group, float spacing,
                                  float interpolationSpeed, Color subordinateColor)
{
    group->followSpacing = spacing;
    group->followInterpolationSpeed = interpolationSpeed;
    group->subordinateColor = subordinateColor;

    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        Enemy_SetFollowSettings(&group->enemies[index], spacing, interpolationSpeed,
                                subordinateColor);
    }
}

float EnemyGroup_GetFollowSpacing(const EnemyGroup *group)
{
    return group->followSpacing;
}

float EnemyGroup_GetFollowInterpolationSpeed(const EnemyGroup *group)
{
    return group->followInterpolationSpeed;
}

Color EnemyGroup_GetSubordinateColor(const EnemyGroup *group)
{
    return group->subordinateColor;
}

void EnemyGroup_SetScale(EnemyGroup *group, float scale)
{
    for (int index = 0; index < ENEMY_GROUP_MAX_COUNT; index++) {
        Enemy_SetScale(&group->enemies[index], scale);
    }
}

float EnemyGroup_GetScale(const EnemyGroup *group)
{
    return Enemy_GetScale(&group->enemies[0]);
}

void EnemyGroup_UnloadAppearance(EnemyGroup *group)
{
    Enemy_UnloadAppearance(&group->enemies[0]);
    for (int index = 1; index < ENEMY_GROUP_MAX_COUNT; index++) {
        group->enemies[index].appearance = (Texture2D){0};
        group->enemies[index].hasAppearance = false;
    }
}
