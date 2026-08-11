// 依存: enemy_settings.h
#include "enemy_settings.h"

#include <stdio.h>
#include <string.h>

EnemyFollowSettings EnemySettings_Default(void)
{
    return (EnemyFollowSettings){
        .spacing = 20.0f,
        .interpolationSpeed = 6.0f,
        .subordinateColor = LIME,
        .playerScale = 1.5f,
        .enemyScale = 1.5f,
        .gridOverlayOpacity = 0.55f,
    };
}

bool EnemySettings_Load(const char *filePath, EnemyFollowSettings *settings)
{
    FILE *file = fopen(filePath, "r");
    int red = 0;
    int green = 0;
    int blue = 0;
    int alpha = 0;
    char line[128];
    bool loadedAnyValue = false;

    if (file == NULL) {
        return false;
    }

    // 行単位で読むことで、拡張前の設定ファイルにも不足分の既定値を適用できる。
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "spacing=%f", &settings->spacing) == 1 ||
            sscanf(line, "interpolation_speed=%f", &settings->interpolationSpeed) == 1 ||
            sscanf(line, "player_scale=%f", &settings->playerScale) == 1 ||
            sscanf(line, "enemy_scale=%f", &settings->enemyScale) == 1 ||
            sscanf(line, "grid_overlay_opacity=%f", &settings->gridOverlayOpacity) == 1) {
            loadedAnyValue = true;
        } else if (sscanf(line, "color=%d,%d,%d,%d", &red, &green, &blue, &alpha) == 4) {
            settings->subordinateColor = (Color){ (unsigned char)red, (unsigned char)green,
                                                  (unsigned char)blue, (unsigned char)alpha };
            loadedAnyValue = true;
        }
    }
    fclose(file);
    return loadedAnyValue;
}

bool EnemySettings_Save(const char *filePath, const EnemyFollowSettings *settings)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) {
        return false;
    }

    fprintf(file, "spacing=%.2f\ninterpolation_speed=%.2f\ncolor=%u,%u,%u,%u\n"
                  "player_scale=%.2f\nenemy_scale=%.2f\ngrid_overlay_opacity=%.2f\n",
            settings->spacing, settings->interpolationSpeed,
            settings->subordinateColor.r, settings->subordinateColor.g,
            settings->subordinateColor.b, settings->subordinateColor.a,
            settings->playerScale, settings->enemyScale, settings->gridOverlayOpacity);
    fclose(file);
    return true;
}
