// 依存する自プロジェクト内ファイル: なし。
// 役割: タイトル、タイトル内メニュー、ゲーム内設定の遷移状態と共通UIを管理する。
#ifndef RPG_SCENE_H
#define RPG_SCENE_H

#include "raylib.h"

enum { RPG_SCENE_MAX_STAGE_BUTTONS = 32 };

typedef enum RpgSceneKind {
    RPG_SCENE_TITLE,
    RPG_SCENE_TITLE_START_MENU,
    RPG_SCENE_TITLE_STAGE_BUILD,
    RPG_SCENE_TITLE_SETTINGS,
    RPG_SCENE_GAME,
    RPG_SCENE_GAME_SETTINGS
} RpgSceneKind;

typedef struct RpgSceneState {
    RpgSceneKind kind;
    // 設定を開く直前の画面を保持し、ゲームの状態を確認できるようにする。
    Texture2D frozenBackdrop;
    bool hasFrozenBackdrop;
    // エディター用設定からはタイトル画面へ遷移させない。
    bool allowsTitleReturn;
    int selectedStageNumber;
    // タイトル画面のビルド一覧。ステージ保存とは独立し、本編起動時に台帳から渡される。
    int stageNumbers[RPG_SCENE_MAX_STAGE_BUTTONS];
    int stageCount;
    int stageButtonScroll;
    // タイトルから本編へ入るたびに、同じステージ番号でも実行状態を作り直す。
    bool requiresGameReset;
    bool requiresContinueLoad;
} RpgSceneState;

RpgSceneState RpgScene_Default(void);
RpgSceneState RpgScene_GameOnly(void);
void RpgScene_Release(RpgSceneState *scene);
void RpgScene_RegisterText(void);
bool RpgScene_IsGameScene(const RpgSceneState *scene);
void RpgScene_SetStageNumber(RpgSceneState *scene, int stageNumber);
void RpgScene_SetStageList(RpgSceneState *scene, const int *stageNumbers, int stageCount);
bool RpgScene_ConsumeGameReset(RpgSceneState *scene);
bool RpgScene_ConsumeContinueLoad(RpgSceneState *scene);
bool RpgScene_IsGameSettings(const RpgSceneState *scene);
void RpgScene_SetGameSettingsButtonBounds(Rectangle bounds);
bool RpgScene_TryOpenGameSettings(RpgSceneState *scene);
bool RpgScene_UpdateGameSettings(RpgSceneState *scene);
void RpgScene_DrawGameSettingsButton(void);
void RpgScene_DrawGameSettingsOverlay(const RpgSceneState *scene);
void RpgScene_UpdateAndDraw(RpgSceneState *scene);

#endif
