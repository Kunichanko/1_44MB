// 依存する自プロジェクト内ファイル: rpg_editor_history.h
#include "rpg_editor_history.h"

#include <stdio.h>
#include <string.h>

enum { RPG_EDITOR_HISTORY_VERSION = 2 };

typedef struct RpgEditorHistoryFileHeader {
    char magic[8];
    int version;
    int count;
} RpgEditorHistoryFileHeader;

void RpgEditorHistory_Push(RpgEditorHistory *history, const RpgEditorState *state)
{
    if (history->count == RPG_EDITOR_HISTORY_CAPACITY) {
        memmove(history->entries, history->entries + 1,
                sizeof(history->entries[0]) * (RPG_EDITOR_HISTORY_CAPACITY - 1));
        history->count--;
    }
    history->entries[history->count++] = *state;
}

bool RpgEditorHistory_Pop(RpgEditorHistory *history, RpgEditorState *state)
{
    if (history->count <= 0) return false;
    *state = history->entries[--history->count];
    return true;
}

bool RpgEditorHistory_Load(const char *filePath, RpgEditorHistory *history)
{
    FILE *file = fopen(filePath, "rb");
    RpgEditorHistoryFileHeader header = { 0 };
    if (file == NULL) return false;
    bool valid = fread(&header, sizeof(header), 1, file) == 1 &&
                 memcmp(header.magic, "RPGUNDO", 7) == 0 &&
                 header.version == RPG_EDITOR_HISTORY_VERSION &&
                 header.count >= 0 && header.count <= RPG_EDITOR_HISTORY_CAPACITY;
    if (valid) valid = fread(history->entries, sizeof(history->entries[0]), (size_t)header.count, file) ==
                       (size_t)header.count;
    fclose(file);
    if (!valid) return false;
    history->count = header.count;
    return true;
}

bool RpgEditorHistory_Save(const char *filePath, const RpgEditorHistory *history)
{
    FILE *file = fopen(filePath, "wb");
    RpgEditorHistoryFileHeader header = { "RPGUNDO", RPG_EDITOR_HISTORY_VERSION, history->count };
    if (file == NULL) return false;
    bool saved = fwrite(&header, sizeof(header), 1, file) == 1 &&
                 fwrite(history->entries, sizeof(history->entries[0]), (size_t)history->count, file) ==
                 (size_t)history->count;
    return fclose(file) == 0 && saved;
}
