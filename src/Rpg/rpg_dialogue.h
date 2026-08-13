// 依存する自プロジェクト内ファイル: なし
#ifndef RPG_DIALOGUE_H
#define RPG_DIALOGUE_H

#include <stdbool.h>

enum {
    RPG_DIALOGUE_MAX_LINES = 64,
    RPG_DIALOGUE_LINE_LENGTH = 192,
    RPG_DIALOGUE_SPEAKER_LENGTH = 64
};

typedef struct RpgDialogue {
    int lineCount;
    char speakers[RPG_DIALOGUE_MAX_LINES][RPG_DIALOGUE_SPEAKER_LENGTH];
    char lines[RPG_DIALOGUE_MAX_LINES][RPG_DIALOGUE_LINE_LENGTH];
} RpgDialogue;

RpgDialogue RpgDialogue_Default(void);
bool RpgDialogue_Load(const char *filePath, RpgDialogue *dialogue);
bool RpgDialogue_Save(const char *filePath, const RpgDialogue *dialogue);
bool RpgDialogue_AddLine(RpgDialogue *dialogue);
bool RpgDialogue_DeleteLine(RpgDialogue *dialogue, int lineIndex);
bool RpgDialogue_MoveLine(RpgDialogue *dialogue, int sourceIndex, int destinationIndex);

#endif
