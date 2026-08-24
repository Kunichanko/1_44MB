// 依存する自プロジェクト内ファイル: rpg_dialogue.h
#ifndef RPG_INSPECT_H
#define RPG_INSPECT_H

#include <stdbool.h>
#include <stdio.h>
#include "rpg_dialogue.h"

enum { RPG_INSPECT_MAX_FUNCTIONS = 16, RPG_INSPECT_TITLE_LENGTH = RPG_DIALOGUE_SPEAKER_LENGTH };

typedef enum RpgInspectFunctionType {
    RPG_INSPECT_DIALOGUE,
    RPG_INSPECT_MOVE,
    RPG_INSPECT_WAIT,
    RPG_INSPECT_LAYER_CHANGE
} RpgInspectFunctionType;

typedef enum RpgInspectMoveTarget {
    RPG_INSPECT_MOVE_PLAYER,
    RPG_INSPECT_MOVE_NPC,
    RPG_INSPECT_MOVE_ZIPPER,
    RPG_INSPECT_MOVE_IMAGE_OBJECT
} RpgInspectMoveTarget;

/* Move Functionで共有する補間方式。保存値は列挙順に固定する。 */
typedef enum RpgInspectMoveEasing {
    RPG_INSPECT_EASING_LINEAR,
    RPG_INSPECT_EASING_QUADRATIC_IN,
    RPG_INSPECT_EASING_QUADRATIC_OUT,
    RPG_INSPECT_EASING_QUADRATIC_IN_OUT,
    RPG_INSPECT_EASING_BOUNCE_OUT,
    RPG_INSPECT_EASING_COUNT
} RpgInspectMoveEasing;

/* Move Functionで動かす座標軸。Xのみを既存データの既定値として保つ。 */
typedef enum RpgInspectMoveAxis {
    RPG_INSPECT_MOVE_AXIS_X,
    RPG_INSPECT_MOVE_AXIS_Y,
    RPG_INSPECT_MOVE_AXIS_XY,
    RPG_INSPECT_MOVE_AXIS_COUNT
} RpgInspectMoveAxis;

typedef struct RpgInspectMove {
    RpgInspectMoveTarget target;
    float destinationX;
    float destinationY;
    float duration;
    /* 開始からこの秒数で次のFunctionへ進む。移動時間より短くても移動は継続する。 */
    float nextFunctionDelay;
    /* Playerのwalkスプライトを使える対象だけに適用する。 */
    bool walkAnimationEnabled;
    float walkAnimationSpeed;
    RpgInspectMoveEasing easing;
    RpgInspectMoveAxis axis;
    bool snapToGrid;
    /* Image Object対象時だけ使用する、配置物固有ID。 */
    unsigned int targetImageObjectId;
} RpgInspectMove;

typedef struct RpgInspectWait {
    float duration;
} RpgInspectWait;

typedef struct RpgInspectLayerChange {
    unsigned int targetImageObjectId;
    int layer;
} RpgInspectLayerChange;

// Functionブロックはタイトルと機能別設定を一体で保持する。
typedef struct RpgInspectFunction {
    char title[RPG_INSPECT_TITLE_LENGTH];
    RpgInspectFunctionType type;
    RpgDialogue dialogue;
    RpgInspectMove move;
    RpgInspectWait wait;
    RpgInspectLayerChange layerChange;
} RpgInspectFunction;

typedef struct RpgInspect {
    bool enabled;
    int functionCount;
    RpgInspectFunction functions[RPG_INSPECT_MAX_FUNCTIONS];
} RpgInspect;

RpgInspect RpgInspect_Default(const char *speaker, const char *text);
/* 0〜1の進行度へ指定補間を適用する。本編・エディターのプレビューで共用する。 */
float RpgInspect_EaseMoveProgress(RpgInspectMoveEasing easing, float progress);
const char *RpgInspect_MoveEasingName(RpgInspectMoveEasing easing);
const char *RpgInspect_MoveAxisName(RpgInspectMoveAxis axis);
bool RpgInspect_MoveAxisHasX(RpgInspectMoveAxis axis);
bool RpgInspect_MoveAxisHasY(RpgInspectMoveAxis axis);
/* 同じ Function 保存形式をステージ・エリアイベントでも再利用する。 */
bool RpgInspect_LoadStream(FILE *file, RpgInspect *inspect);
bool RpgInspect_SaveStream(FILE *file, const RpgInspect *inspect);
bool RpgInspect_Load(const char *filePath, RpgInspect *inspect);
bool RpgInspect_Save(const char *filePath, const RpgInspect *inspect);
#endif
// 役割: 調べる Function 列の構造と保存 API を宣言する。
