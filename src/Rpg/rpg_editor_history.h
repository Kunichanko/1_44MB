// 依存する自プロジェクト内ファイル: rpg_character.h, rpg_dialogue.h, rpg_inspect.h, rpg_stage.h, rpg_stage3_event.h, rpg_zipper.h
#ifndef RPG_EDITOR_HISTORY_H
#define RPG_EDITOR_HISTORY_H

#include <stdbool.h>

#include "rpg_character.h"
#include "rpg_dialogue.h"
#include "rpg_inspect.h"
#include "rpg_stage.h"
#include "rpg_stage3_event.h"
#include "rpg_zipper.h"

enum { RPG_EDITOR_HISTORY_CAPACITY = 50 };

typedef struct RpgEditorState {
    RpgCharacter player;
    RpgCharacter npc;
    RpgStage stage;
    RpgDialogue dialogue;
    RpgStage3Event stage3Event;
    RpgZipper zipper;
    RpgInspect npcInspectSnapshot;
    RpgInspect zipperInspectSnapshot;
    int mapIndex;
    int selected;
    int activeInspectKind;
    bool isDialogueEditorOpen;
    bool isExamineFunctionListOpen;
    bool isFunctionTypeListOpen;
    bool isMoveFunctionEditorOpen;
    bool isStage3DialogueEditing;
    bool isInspectDialogueEditing;
} RpgEditorState;

typedef struct RpgEditorHistory {
    int count;
    RpgEditorState entries[RPG_EDITOR_HISTORY_CAPACITY];
} RpgEditorHistory;

void RpgEditorHistory_Push(RpgEditorHistory *history, const RpgEditorState *state);
bool RpgEditorHistory_Pop(RpgEditorHistory *history, RpgEditorState *state);
bool RpgEditorHistory_Load(const char *filePath, RpgEditorHistory *history);
bool RpgEditorHistory_Save(const char *filePath, const RpgEditorHistory *history);

#endif
