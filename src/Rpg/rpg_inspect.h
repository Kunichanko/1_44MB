// 依存する自プロジェクト内ファイル: rpg_dialogue.h
#ifndef RPG_INSPECT_H
#define RPG_INSPECT_H

#include <stdbool.h>
#include "rpg_dialogue.h"

enum { RPG_INSPECT_MAX_FUNCTIONS = 16, RPG_INSPECT_TITLE_LENGTH = RPG_DIALOGUE_SPEAKER_LENGTH };

typedef enum RpgInspectFunctionType {
    RPG_INSPECT_DIALOGUE,
    RPG_INSPECT_MOVE
} RpgInspectFunctionType;

typedef enum RpgInspectMoveTarget {
    RPG_INSPECT_MOVE_PLAYER,
    RPG_INSPECT_MOVE_NPC,
    RPG_INSPECT_MOVE_ZIPPER
} RpgInspectMoveTarget;

typedef struct RpgInspectMove {
    RpgInspectMoveTarget target;
    float destinationX;
    float duration;
} RpgInspectMove;

// Functionブロックはタイトルと機能別設定を一体で保持する。
typedef struct RpgInspectFunction {
    char title[RPG_INSPECT_TITLE_LENGTH];
    RpgInspectFunctionType type;
    RpgDialogue dialogue;
    RpgInspectMove move;
} RpgInspectFunction;

typedef struct RpgInspect {
    bool enabled;
    int functionCount;
    RpgInspectFunction functions[RPG_INSPECT_MAX_FUNCTIONS];
} RpgInspect;

RpgInspect RpgInspect_Default(const char *speaker, const char *text);
bool RpgInspect_Load(const char *filePath, RpgInspect *inspect);
bool RpgInspect_Save(const char *filePath, const RpgInspect *inspect);
#endif
