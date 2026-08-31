// 依存する自プロジェクト内ファイル: rpg_layout.h
#include "rpg_layout.h"
#include "rpg_stage.h"

#include <stdio.h>

/* Runtimeの共有設定はステージのレイアウトと分離し、常に一つの値を利用する。 */
static const char *GetGlobalRuntimePath(void)
{
    return TextFormat("%s../assets/Settings/rpg_global_runtime.cfg", GetApplicationDirectory());
}

static void NormalizeGlobalRuntime(RpgLayout *layout)
{
    if (layout->electricCellDelay < 0.01f || layout->electricCellDelay > 2.0f)
        layout->electricCellDelay = 0.15f;
    if (layout->magnetMetalSpeed < 16.0f || layout->magnetMetalSpeed > 960.0f)
        layout->magnetMetalSpeed = 160.0f;
    if (layout->zipperFolderReturnDuration < 0.10f || layout->zipperFolderReturnDuration > 5.0f)
        layout->zipperFolderReturnDuration = 0.45f;
    if (layout->zipperFolderReturnAnimationDelay < 0.0f || layout->zipperFolderReturnAnimationDelay > 5.0f)
        layout->zipperFolderReturnAnimationDelay = 0.0f;
    if (layout->referenceFollowerScale < 0.15f || layout->referenceFollowerScale > 1.0f)
        layout->referenceFollowerScale = 0.50f;
}

static void NormalizeStageVisuals(RpgLayout *layout)
{
    if (layout->backgroundBrightness < 0.15f || layout->backgroundBrightness > 1.0f)
        layout->backgroundBrightness = 1.0f;
    if (layout->blockBrightness < 0.15f || layout->blockBrightness > 1.0f)
        layout->blockBrightness = 1.0f;
    if (layout->zipperMaxCapacityKB == 0 || layout->zipperMaxCapacityKB > 1048576U)
        layout->zipperMaxCapacityKB = 10U;
}

RpgLayout RpgLayout_Default(void)
{
    // 足元をマス下辺中央に揃え、初期配置とエディターのスナップ規則を一致させる。
    return (RpgLayout){ .playerPosition = { 8.5f * RPG_STAGE_TILE_SIZE, RPG_STAGE_GROUND_TOP },
                         .npcPosition = { 12.5f * RPG_STAGE_TILE_SIZE, RPG_STAGE_GROUND_TOP },
                         .playerMoveSpeed = 180.0f, .playerScale = 1.0f, .npcScale = 1.0f,
                          .stage3IntroEnabled = true, .electricCellDelay = 0.15f, .magnetMetalSpeed = 160.0f,
                          .backgroundBrightness = 1.0f, .blockBrightness = 1.0f,
                          .zipperMaxCapacityKB = 10U,
                         .zipperFolderReturnDuration = 0.45f, .zipperFolderReturnAnimationDelay = 0.0f,
                         .referenceFollowerScale = 0.50f };
}

bool RpgLayout_Load(const char *filePath, RpgLayout *layout)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) return false;
    int stage3IntroEnabled = 1;
    char layoutLine[256];
    // 数値の書式変更があっても次行の background= を fscanf がまたいで消費しないよう、
    // レイアウト数値は必ず一行だけを解析する。
    if (fgets(layoutLine, sizeof(layoutLine), file) == NULL) {
        fclose(file);
        return false;
    }
    int readCount = sscanf(layoutLine, "%f %f %f %f %f %f %f %d %f %f %f", &layout->playerPosition.x,
                           &layout->playerPosition.y, &layout->npcPosition.x,
                           &layout->npcPosition.y, &layout->playerMoveSpeed, &layout->playerScale,
                           &layout->npcScale, &stage3IntroEnabled, &layout->electricCellDelay,
                           &layout->zipperFolderReturnDuration, &layout->zipperFolderReturnAnimationDelay);
    layout->backgroundPath[0] = '\0';
    layout->backgroundBrightness = 1.0f;
    layout->blockBrightness = 1.0f;
    layout->zipperMaxCapacityKB = 10U;
    char line[RPG_LAYOUT_BACKGROUND_PATH_LENGTH + 16];
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "background=%511[^\r\n]", layout->backgroundPath) == 1) continue;
        if (sscanf(line, "background_brightness=%f", &layout->backgroundBrightness) == 1) continue;
        if (sscanf(line, "block_brightness=%f", &layout->blockBrightness) == 1) continue;
        (void)sscanf(line, "zipper_max_capacity_kb=%u", &layout->zipperMaxCapacityKB);
    }
    fclose(file);
    if (readCount == 4) layout->playerMoveSpeed = 180.0f;
    if (readCount < 6) layout->playerScale = 1.0f;
    if (readCount < 7) layout->npcScale = 1.0f;
    if (readCount < 9) layout->electricCellDelay = 0.15f;
    if (readCount < 10) layout->zipperFolderReturnDuration = 0.45f;
    if (readCount < 11) layout->zipperFolderReturnAnimationDelay = 0.0f;
    NormalizeGlobalRuntime(layout);
    NormalizeStageVisuals(layout);
    /* 旧グリッドの足元座標は新しい8行グリッド外になるため、標準の下辺中央へ移し替える。 */
    if (layout->playerPosition.y > RPG_STAGE_WORLD_HEIGHT || layout->npcPosition.y > RPG_STAGE_WORLD_HEIGHT) {
        RpgLayout defaults = RpgLayout_Default();
        layout->playerPosition = defaults.playerPosition;
        layout->npcPosition = defaults.npcPosition;
    }
    layout->stage3IntroEnabled = stage3IntroEnabled != 0;
    return readCount >= 4;
}

bool RpgLayout_Save(const char *filePath, const RpgLayout *layout)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    // ステージ固有の配置だけを書き出す。Runtime値はrpg_global_runtime.cfgだけが保持する。
    fprintf(file, "%.1f %.1f %.1f %.1f %.1f %.2f %.2f %d\n", layout->playerPosition.x,
            layout->playerPosition.y, layout->npcPosition.x, layout->npcPosition.y,
            layout->playerMoveSpeed, layout->playerScale, layout->npcScale,
            layout->stage3IntroEnabled ? 1 : 0);
    fprintf(file, "background=%s\n", layout->backgroundPath);
    fprintf(file, "background_brightness=%.2f\n", layout->backgroundBrightness);
    fprintf(file, "block_brightness=%.2f\n", layout->blockBrightness);
    fprintf(file, "zipper_max_capacity_kb=%u\n", layout->zipperMaxCapacityKB);
    return fclose(file) == 0;
}

bool RpgLayout_LoadGlobalRuntime(RpgLayout *layout)
{
    if (layout == NULL) return false;
    FILE *file = fopen(GetGlobalRuntimePath(), "r");
    // 既存ステージの値を初回だけ共有設定へ移し、以後はステージ別の値を参照しない。
    if (file == NULL) return RpgLayout_SaveGlobalRuntime(layout);
    int readCount = fscanf(file, "%f %f %f %f %f", &layout->electricCellDelay,
                           &layout->zipperFolderReturnDuration,
                           &layout->zipperFolderReturnAnimationDelay, &layout->referenceFollowerScale,
                           &layout->magnetMetalSpeed);
    fclose(file);
    if (readCount < 3) return RpgLayout_SaveGlobalRuntime(layout);
    if (readCount < 4) layout->referenceFollowerScale = 0.50f;
    if (readCount < 5) layout->magnetMetalSpeed = 160.0f;
    NormalizeGlobalRuntime(layout);
    return true;
}

bool RpgLayout_SaveGlobalRuntime(const RpgLayout *layout)
{
    if (layout == NULL) return false;
    FILE *file = fopen(GetGlobalRuntimePath(), "w");
    if (file == NULL) return false;
    fprintf(file, "%.3f %.2f %.2f %.2f %.1f\n", layout->electricCellDelay,
            layout->zipperFolderReturnDuration, layout->zipperFolderReturnAnimationDelay,
            layout->referenceFollowerScale, layout->magnetMetalSpeed);
    return fclose(file) == 0;
}
// 役割: RPG のキャラクター配置と全体レイアウト設定を保存・読み込みする。
