// 依存する自プロジェクト内ファイル: rpg_stage3_event.h
#include "rpg_stage3_event.h"

#include <stdio.h>
#include <string.h>

/* 旧会話専用データを、共通 Function 形式の最初の Dialogue へ移行する。 */
static void SyncInspectFromLegacyDialogue(RpgStage3Event *event)
{
    event->inspect = RpgInspect_Default(event->dialogue.speakers[0], event->dialogue.lines[0]);
    event->inspect.enabled = event->enabled;
    event->inspect.functions[0].dialogue = event->dialogue;
}

RpgStage3Event RpgStage3Event_Default(void)
{
    RpgStage3Event event = { .enabled = false };
    event.dialogue.lineCount = 1;
    strcpy(event.dialogue.speakers[0], "Zipper");
    strcpy(event.dialogue.lines[0], "ようこそ、最後のステージへ。");
    SyncInspectFromLegacyDialogue(&event);
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
    if (strncmp(header, "v4", 2) == 0) {
        bool loaded = RpgInspect_LoadStream(file, &event->inspect);
        event->enabled = event->inspect.enabled;
        fclose(file);
        return loaded;
    }
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
            SyncInspectFromLegacyDialogue(event);
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
        SyncInspectFromLegacyDialogue(event);
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
    fprintf(file, "v4\n");
    bool saved = RpgInspect_SaveStream(file, &event->inspect);
    return fclose(file) == 0 && saved;
}

void RpgAreaEntryEvents_Initialize(RpgAreaEntryEvents *events)
{
    if (events == NULL) return;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
        events->entries[index] = RpgStage3Event_Default();
}

bool RpgAreaEntryEvents_Load(const char *filePath, RpgAreaEntryEvents *events)
{
    FILE *file = fopen(filePath, "r");
    char line[RPG_DIALOGUE_SPEAKER_LENGTH + RPG_DIALOGUE_LINE_LENGTH + 48];
    int version = 0;
    if (file == NULL || events == NULL) { if (file != NULL) fclose(file); return false; }
    if (fscanf(file, "v%d\n", &version) != 1 || (version != 1 && version != 2)) { fclose(file); return false; }
    RpgAreaEntryEvents_Initialize(events);
    while (fgets(line, sizeof(line), file) != NULL) {
        int index = -1, enabled = 0, count = 0;
        if (version == 2) {
            if (sscanf(line, "area %d", &index) != 1 || index < 0 || index >= RPG_STAGE_MAP_COUNT) break;
            RpgStage3Event *event = &events->entries[index];
            if (!RpgInspect_LoadStream(file, &event->inspect)) break;
            event->enabled = event->inspect.enabled;
            continue;
        }
        if (sscanf(line, "area %d %d %d", &index, &enabled, &count) != 3 || index < 0 ||
            index >= RPG_STAGE_MAP_COUNT || count < 1 || count > RPG_DIALOGUE_MAX_LINES) break;
        RpgStage3Event *event = &events->entries[index];
        event->enabled = enabled != 0;
        event->dialogue.lineCount = 0;
        for (int lineIndex = 0; lineIndex < count; lineIndex++) {
            char *separator;
            if (fgets(line, sizeof(line), file) == NULL) break;
            separator = strchr(line, '\t');
            if (separator == NULL) continue;
            *separator++ = '\0';
            line[strcspn(line, "\r\n")] = '\0';
            separator[strcspn(separator, "\r\n")] = '\0';
            strcpy(event->dialogue.speakers[event->dialogue.lineCount], line);
            strcpy(event->dialogue.lines[event->dialogue.lineCount], separator);
            event->dialogue.lineCount++;
        }
        if (event->dialogue.lineCount == 0) event->dialogue = RpgStage3Event_Default().dialogue;
        SyncInspectFromLegacyDialogue(event);
    }
    fclose(file);
    return true;
}

bool RpgAreaEntryEvents_Save(const char *filePath, const RpgAreaEntryEvents *events)
{
    FILE *file;
    if (events == NULL || (file = fopen(filePath, "w")) == NULL) return false;
    fprintf(file, "v2\n");
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        const RpgStage3Event *event = &events->entries[index];
        fprintf(file, "area %d\n", index);
        if (!RpgInspect_SaveStream(file, &event->inspect)) { fclose(file); return false; }
    }
    return fclose(file) == 0;
}
// 役割: ステージ3へ入った時に一度だけ起動する会話イベントを管理する。
