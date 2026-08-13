// 依存する自プロジェクト内ファイル: rpg_inspect.h
#include "rpg_inspect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void SetDefaultDialogue(RpgInspect *inspect, int index, const char *speaker, const char *text)
{
    inspect->functions[index].type = RPG_INSPECT_DIALOGUE;
    snprintf(inspect->functions[index].title, RPG_INSPECT_TITLE_LENGTH, "Dialogue%d", index + 1);
    inspect->functions[index].dialogue.lineCount = 1;
    strcpy(inspect->functions[index].dialogue.speakers[0], speaker);
    strcpy(inspect->functions[index].dialogue.lines[0], text);
}

RpgInspect RpgInspect_Default(const char *speaker, const char *text)
{
    RpgInspect inspect = { .enabled = false, .functionCount = 1 };
    SetDefaultDialogue(&inspect, 0, speaker, text);
    return inspect;
}

static bool LoadDialogueLines(FILE *file, RpgDialogue *dialogue, int count)
{
    char buffer[RPG_DIALOGUE_SPEAKER_LENGTH + RPG_DIALOGUE_LINE_LENGTH + 2];
    dialogue->lineCount = 0;
    for (int line = 0; line < count && line < RPG_DIALOGUE_MAX_LINES; line++) {
        if (fgets(buffer, sizeof(buffer), file) == NULL) break;
        char *tab = strchr(buffer, '\t');
        if (tab == NULL) continue;
        *tab++ = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        tab[strcspn(tab, "\r\n")] = '\0';
        strcpy(dialogue->speakers[dialogue->lineCount], buffer);
        strcpy(dialogue->lines[dialogue->lineCount++], tab);
    }
    return dialogue->lineCount > 0;
}

bool RpgInspect_Load(const char *filePath, RpgInspect *inspect)
{
    FILE *file = fopen(filePath, "r");
    int enabled = 0, count = 0;
    if (file == NULL || fscanf(file, "%d %d\n", &enabled, &count) != 2) { if (file) fclose(file); return false; }
    inspect->enabled = enabled != 0;
    inspect->functionCount = 0;
    for (int sourceIndex = 0; sourceIndex < count && inspect->functionCount < RPG_INSPECT_MAX_FUNCTIONS; sourceIndex++) {
        char buffer[RPG_DIALOGUE_SPEAKER_LENGTH + RPG_DIALOGUE_LINE_LENGTH + 2];
        if (fgets(buffer, sizeof(buffer), file) == NULL) break;
        int index = inspect->functionCount;
        RpgInspectFunction *function = &inspect->functions[index];
        RpgDialogue *dialogue = &function->dialogue;
        if (buffer[1] == '\t' && (buffer[0] == 'D' || buffer[0] == 'M')) {
            char *type = strtok(buffer, "\t\r\n");
            char *title = strtok(NULL, "\t\r\n");
            char *value = strtok(NULL, "\t\r\n");
            if (title == NULL) title = "Function";
            strncpy(function->title, title, RPG_INSPECT_TITLE_LENGTH - 1);
            function->title[RPG_INSPECT_TITLE_LENGTH - 1] = '\0';
            if (type[0] == 'M') {
                int target = 0;
                float destinationX = 0.0f, duration = 1.0f;
                if (value != NULL) sscanf(value, "%d %f %f", &target, &destinationX, &duration);
                function->type = RPG_INSPECT_MOVE;
                function->move = (RpgInspectMove){ (RpgInspectMoveTarget)target, destinationX, duration };
                if (function->move.target < RPG_INSPECT_MOVE_PLAYER || function->move.target > RPG_INSPECT_MOVE_ZIPPER)
                    function->move.target = RPG_INSPECT_MOVE_PLAYER;
                if (function->move.duration < 0.1f) function->move.duration = 1.0f;
            } else {
                int lineCount = value != NULL ? atoi(value) : 1;
                function->type = RPG_INSPECT_DIALOGUE;
                if (!LoadDialogueLines(file, dialogue, lineCount)) SetDefaultDialogue(inspect, index, "Inspect", "");
            }
        } else {
            // 旧形式のDialogue設定を読み込み、新形式のDialogueとして扱う。
            char *tab = strchr(buffer, '\t');
            if (tab != NULL) {
                *tab++ = '\0';
                char *end = NULL;
                long lineCount = strtol(tab, &end, 10);
                if (end != tab && (*end == '\r' || *end == '\n' || *end == '\0')) {
                    buffer[strcspn(buffer, "\r\n")] = '\0';
                    strncpy(function->title, buffer, RPG_INSPECT_TITLE_LENGTH - 1);
                    function->title[RPG_INSPECT_TITLE_LENGTH - 1] = '\0';
                    function->type = RPG_INSPECT_DIALOGUE;
                    if (!LoadDialogueLines(file, dialogue, (int)lineCount)) SetDefaultDialogue(inspect, index, "Inspect", "");
                } else {
                    SetDefaultDialogue(inspect, index, buffer, tab);
                    dialogue->lines[0][strcspn(dialogue->lines[0], "\r\n")] = '\0';
                }
            } else {
                SetDefaultDialogue(inspect, index, "Inspect", "");
            }
        }
        inspect->functionCount++;
    }
    fclose(file);
    return inspect->functionCount > 0;
}

bool RpgInspect_Save(const char *filePath, const RpgInspect *inspect)
{
    FILE *file = fopen(filePath, "w");
    if (!file) return false;
    fprintf(file, "%d %d\n", inspect->enabled ? 1 : 0, inspect->functionCount);
    for (int index = 0; index < inspect->functionCount; index++) {
        const RpgInspectFunction *function = &inspect->functions[index];
        if (function->type == RPG_INSPECT_MOVE) {
            const RpgInspectMove *move = &function->move;
            fprintf(file, "M\t%s\t%d %.2f %.2f\n", function->title, move->target,
                    move->destinationX, move->duration);
        } else {
            const RpgDialogue *dialogue = &function->dialogue;
            fprintf(file, "D\t%s\t%d\n", function->title, dialogue->lineCount);
            for (int line = 0; line < dialogue->lineCount; line++)
                fprintf(file, "%s\t%s\n", dialogue->speakers[line], dialogue->lines[line]);
        }
    }
    return fclose(file) == 0;
}
