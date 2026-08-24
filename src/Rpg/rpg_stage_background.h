// 役割: ステージごとに指定された背景PNGの読み込み・描画・解放を管理する。
// 依存する自プロジェクト内ファイル: rpg_layout.h
#ifndef RPG_STAGE_BACKGROUND_H
#define RPG_STAGE_BACKGROUND_H

#include "raylib.h"

#include "rpg_layout.h"

typedef struct RpgStageBackground {
    Texture2D texture;
    char loadedPath[RPG_LAYOUT_BACKGROUND_PATH_LENGTH];
} RpgStageBackground;

RpgStageBackground RpgStageBackground_Default(void);
bool RpgStageBackground_Load(RpgStageBackground *background, const char *path);
void RpgStageBackground_Unload(RpgStageBackground *background);
// 描画先はゲーム画面ではなく、対応するステージのワールド領域を指定する。
void RpgStageBackground_Draw(const RpgStageBackground *background, Rectangle destination,
                             float brightness);

#endif
