// 依存: enemy.h
#ifndef ENEMY_GROUP_H
#define ENEMY_GROUP_H

#include "enemy.h"

enum {
    ENEMY_GROUP_MAX_COUNT = 3,
    ENEMY_GROUP_POSITION_HISTORY_COUNT = 120,
};

typedef struct EnemyGroup {
    Enemy enemies[ENEMY_GROUP_MAX_COUNT];
    float followSpacing;
    float followInterpolationSpeed;
    Color subordinateColor;
    Vector2 leaderPositionHistory[ENEMY_GROUP_POSITION_HISTORY_COUNT];
    float leaderPositionAge[ENEMY_GROUP_POSITION_HISTORY_COUNT];
    int leaderPositionHistoryCount;
} EnemyGroup;

EnemyGroup EnemyGroup_Create(float groundY);
void EnemyGroup_LoadAppearance(EnemyGroup *group, const char *filePath);
bool EnemyGroup_TrySubordinateNearest(EnemyGroup *group, Vector2 leaderPosition,
                                      int leaderDirection, float leaderScale, float actionRange);
void EnemyGroup_Update(EnemyGroup *group, float deltaTime, Vector2 leaderPosition,
                       int leaderDirection, float leaderScale, float leaderMoveSpeed);
void EnemyGroup_Draw(const EnemyGroup *group, float groundY);
int EnemyGroup_GetSubordinateCount(const EnemyGroup *group);
void EnemyGroup_SetFollowSettings(EnemyGroup *group, float spacing,
                                  float interpolationSpeed, Color subordinateColor);
float EnemyGroup_GetFollowSpacing(const EnemyGroup *group);
float EnemyGroup_GetFollowInterpolationSpeed(const EnemyGroup *group);
Color EnemyGroup_GetSubordinateColor(const EnemyGroup *group);
void EnemyGroup_SetScale(EnemyGroup *group, float scale);
float EnemyGroup_GetScale(const EnemyGroup *group);
void EnemyGroup_UnloadAppearance(EnemyGroup *group);

#endif
