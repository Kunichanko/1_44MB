// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_LAYOUT_H
#define RPG_LAYOUT_H

#include "raylib.h"

enum { RPG_LAYOUT_BACKGROUND_PATH_LENGTH = 512 };

typedef struct RpgLayout {
    Vector2 playerPosition;
    Vector2 npcPosition;
    float playerMoveSpeed;
    float playerScale;
    float npcScale;
    bool stage3IntroEnabled;
    /* ステージ固有の背景PNG。空文字なら背景を描画しない。 */
    char backgroundPath[RPG_LAYOUT_BACKGROUND_PATH_LENGTH];
    /* ステージ固有の見た目。0.15〜1.00で背景とブロックを個別に暗くできる。 */
    float backgroundBrightness;
    float blockBrightness;
    float electricCellDelay;
    /* Zipper が返却フォルダを元位置へ運ぶ演出の秒数。全ステージ共有のRuntime設定として保存する。 */
    float zipperFolderReturnDuration;
    /* 返却フォルダを待機場所へ移してから、返却演出を開始するまでの全体共有の待機秒数。 */
    float zipperFolderReturnAnimationDelay;
    /* Gで取得したFile.pngの追従時スケール。全ステージ共有のRuntime設定として保存する。 */
    float referenceFollowerScale;
} RpgLayout;

RpgLayout RpgLayout_Default(void);
bool RpgLayout_Load(const char *filePath, RpgLayout *layout);
bool RpgLayout_Save(const char *filePath, const RpgLayout *layout);
/* 全ステージ共有のRuntime設定をSettings直下へ保存・復元する。 */
bool RpgLayout_LoadGlobalRuntime(RpgLayout *layout);
bool RpgLayout_SaveGlobalRuntime(const RpgLayout *layout);

#endif
// 役割: RPG レイアウト設定の構造と保存 API を宣言する。
