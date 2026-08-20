// 依存する自プロジェクト内ファイル: rpg_dialogue.h
#include "rpg_dialogue.h"

#include <stdio.h>
#include <string.h>

RpgDialogue RpgDialogue_Default(void)
{
    RpgDialogue dialogue = { 0 };
    dialogue.lineCount = 3;
    strcpy(dialogue.speakers[0], "NPC");
    strcpy(dialogue.speakers[1], "NPC");
    strcpy(dialogue.speakers[2], "NPC");
    strcpy(dialogue.lines[0], "RPGプロトタイプへようこそ！");
    strcpy(dialogue.lines[1], "この世界には三つのマップがあります。");
    strcpy(dialogue.lines[2], "Cキーでカメラモードを切り替えられます。");
    return dialogue;
}

bool RpgDialogue_Load(const char *filePath, RpgDialogue *dialogue)
{
    FILE *file = fopen(filePath, "r");
    char line[RPG_DIALOGUE_LINE_LENGTH];
    int count = 0;
    if (file == NULL) return false;

    while (count < RPG_DIALOGUE_MAX_LINES && fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *separator = strchr(line, '\t');
        if (separator != NULL) {
            *separator = '\0';
            strncpy(dialogue->speakers[count], line, RPG_DIALOGUE_SPEAKER_LENGTH - 1);
            dialogue->speakers[count][RPG_DIALOGUE_SPEAKER_LENGTH - 1] = '\0';
            separator++;
        } else {
            strcpy(dialogue->speakers[count], "NPC");
            separator = line;
        }
        strncpy(dialogue->lines[count], separator, RPG_DIALOGUE_LINE_LENGTH - 1);
        dialogue->lines[count][RPG_DIALOGUE_LINE_LENGTH - 1] = '\0';
        count++;
    }
    fclose(file);
    if (count > 0) dialogue->lineCount = count;
    return true;
}

bool RpgDialogue_Save(const char *filePath, const RpgDialogue *dialogue)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    for (int index = 0; index < dialogue->lineCount; index++) {
        fprintf(file, "%s\t%s\n", dialogue->speakers[index], dialogue->lines[index]);
    }
    fclose(file);
    return true;
}

bool RpgDialogue_AddLine(RpgDialogue *dialogue)
{
    if (dialogue->lineCount >= RPG_DIALOGUE_MAX_LINES) return false;
    dialogue->lines[dialogue->lineCount][0] = '\0';
    strcpy(dialogue->speakers[dialogue->lineCount], "NPC");
    dialogue->lineCount++;
    return true;
}

bool RpgDialogue_DeleteLine(RpgDialogue *dialogue, int lineIndex)
{
    if (dialogue->lineCount <= 1 || lineIndex < 0 || lineIndex >= dialogue->lineCount) return false;
    for (int index = lineIndex; index < dialogue->lineCount - 1; index++) {
        memcpy(dialogue->lines[index], dialogue->lines[index + 1], RPG_DIALOGUE_LINE_LENGTH);
        memcpy(dialogue->speakers[index], dialogue->speakers[index + 1], RPG_DIALOGUE_SPEAKER_LENGTH);
    }
    dialogue->lineCount--;
    dialogue->lines[dialogue->lineCount][0] = '\0';
    dialogue->speakers[dialogue->lineCount][0] = '\0';
    return true;
}

bool RpgDialogue_MoveLine(RpgDialogue *dialogue, int sourceIndex, int destinationIndex)
{
    char movedLine[RPG_DIALOGUE_LINE_LENGTH];
    char movedSpeaker[RPG_DIALOGUE_SPEAKER_LENGTH];
    if (sourceIndex < 0 || sourceIndex >= dialogue->lineCount || destinationIndex < 0 ||
        destinationIndex >= dialogue->lineCount) return false;
    if (sourceIndex == destinationIndex) return true;
    memcpy(movedLine, dialogue->lines[sourceIndex], sizeof(movedLine));
    memcpy(movedSpeaker, dialogue->speakers[sourceIndex], sizeof(movedSpeaker));
    if (sourceIndex < destinationIndex) {
        for (int index = sourceIndex; index < destinationIndex; index++) {
            memcpy(dialogue->lines[index], dialogue->lines[index + 1], RPG_DIALOGUE_LINE_LENGTH);
            memcpy(dialogue->speakers[index], dialogue->speakers[index + 1], RPG_DIALOGUE_SPEAKER_LENGTH);
        }
    } else {
        for (int index = sourceIndex; index > destinationIndex; index--) {
            memcpy(dialogue->lines[index], dialogue->lines[index - 1], RPG_DIALOGUE_LINE_LENGTH);
            memcpy(dialogue->speakers[index], dialogue->speakers[index - 1], RPG_DIALOGUE_SPEAKER_LENGTH);
        }
    }
    memcpy(dialogue->lines[destinationIndex], movedLine, sizeof(movedLine));
    memcpy(dialogue->speakers[destinationIndex], movedSpeaker, sizeof(movedSpeaker));
    return true;
}
// 役割: 会話データの初期化、読み書き、編集用の操作を管理する。
