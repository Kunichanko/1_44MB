// 依存する自プロジェクト内ファイル: rpg_stage3_event.h
#include "rpg_stage3_event.h"

#include <stdio.h>
#include <string.h>

RpgStage3Event RpgStage3Event_Default(void)
{
    RpgStage3Event event = { .enabled = true };
    event.dialogue.lineCount = 1;
    strcpy(event.dialogue.speakers[0], "Zipper");
    strcpy(event.dialogue.lines[0], "ようこそ、最後のステージへ。");
    return event;
}

bool RpgStage3Event_Load(const char *filePath, RpgStage3Event *event)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) return false;
    int enabled = 1;
    int lineCount = 0;
    char header[128];
    if (fgets(header, sizeof(header), file) == NULL) { fclose(file); return false; }
    int readCount = sscanf(header, "v3 %d %d", &enabled, &lineCount);
    if (readCount != 2) readCount = sscanf(header, "v2 %d %*f %*f %d", &enabled, &lineCount);
    if (readCount != 2) readCount = sscanf(header, "%d %*f %*f %d", &enabled, &lineCount);
    if (readCount == 1) {
        event->enabled = enabled != 0;
        if (fgets(event->dialogue.speakers[0], RPG_DIALOGUE_SPEAKER_LENGTH, file) != NULL &&
            fgets(event->dialogue.lines[0], RPG_DIALOGUE_LINE_LENGTH, file) != NULL) {
            event->dialogue.lineCount = 1;
            event->dialogue.speakers[0][strcspn(event->dialogue.speakers[0], "\r\n")] = '\0';
            event->dialogue.lines[0][strcspn(event->dialogue.lines[0], "\r\n")] = '\0';
            fclose(file);
            return true;
        }
    }
    if (readCount == 2 && lineCount > 0 && lineCount <= RPG_DIALOGUE_MAX_LINES) {
        event->enabled = enabled != 0;
        event->dialogue.lineCount = 0;
        for (int index = 0; index < lineCount; index++) {
            char line[RPG_DIALOGUE_SPEAKER_LENGTH + RPG_DIALOGUE_LINE_LENGTH + 2];
            if (fgets(line, sizeof(line), file) == NULL) break;
            char *separator = strchr(line, '\t');
            if (separator == NULL) continue;
            *separator = '\0';
            separator++;
            line[strcspn(line, "\r\n")] = '\0';
            separator[strcspn(separator, "\r\n")] = '\0';
            strcpy(event->dialogue.speakers[event->dialogue.lineCount], line);
            strcpy(event->dialogue.lines[event->dialogue.lineCount], separator);
            event->dialogue.lineCount++;
        }
        fclose(file);
        return event->dialogue.lineCount > 0;
    }
    fclose(file);
    return false;
}

bool RpgStage3Event_Save(const char *filePath, const RpgStage3Event *event)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "v3 %d %d\n", event->enabled ? 1 : 0, event->dialogue.lineCount);
    for (int index = 0; index < event->dialogue.lineCount; index++) {
        fprintf(file, "%s\t%s\n", event->dialogue.speakers[index], event->dialogue.lines[index]);
    }
    return fclose(file) == 0;
}
