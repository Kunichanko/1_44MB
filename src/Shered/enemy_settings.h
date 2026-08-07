// 依存: なし（raylib は外部ライブラリ）
#ifndef ENEMY_SETTINGS_H
#define ENEMY_SETTINGS_H

#include "raylib.h"

#include <stdbool.h>

typedef struct EnemyFollowSettings {
    float spacing;
    float interpolationSpeed;
    Color subordinateColor;
    float playerScale;
    float enemyScale;
} EnemyFollowSettings;

EnemyFollowSettings EnemySettings_Default(void);
bool EnemySettings_Load(const char *filePath, EnemyFollowSettings *settings);
bool EnemySettings_Save(const char *filePath, const EnemyFollowSettings *settings);

#endif
