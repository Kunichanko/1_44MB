// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_LAYOUT_H
#define RPG_LAYOUT_H

#include "raylib.h"

typedef struct RpgLayout {
    Vector2 playerPosition;
    Vector2 npcPosition;
    float playerMoveSpeed;
    float playerScale;
    float npcScale;
    bool stage3IntroEnabled;
    float electricCellDelay;
} RpgLayout;

RpgLayout RpgLayout_Default(void);
bool RpgLayout_Load(const char *filePath, RpgLayout *layout);
bool RpgLayout_Save(const char *filePath, const RpgLayout *layout);

#endif
// 役割: RPG レイアウト設定の構造と保存 API を宣言する。
