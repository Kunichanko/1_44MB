// 依存する自プロジェクト内ファイル: rpg_item.h
#include "rpg_item.h"
#include "rpg_stage.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

RpgItems RpgItems_Default(void) { return (RpgItems){ 0 }; }
bool RpgItems_Load(const char *filePath, RpgItems *items)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) return false;
    if (fscanf(file, "%d", &items->count) != 1 || items->count < 0 || items->count > RPG_ITEM_MAX_COUNT) { fclose(file); return false; }
    for (int index = 0; index < items->count; index++) {
        if (fscanf(file, "%f %f %63s", &items->entries[index].position.x, &items->entries[index].position.y,
                   items->entries[index].name) != 3) { fclose(file); return false; }
        items->entries[index].collected = false;
    }
    return fclose(file) == 0;
}
bool RpgItems_Save(const char *filePath, const RpgItems *items)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    fprintf(file, "%d\n", items->count);
    for (int index = 0; index < items->count; index++)
        fprintf(file, "%.1f %.1f %s\n", items->entries[index].position.x, items->entries[index].position.y, items->entries[index].name);
    return fclose(file) == 0;
}
bool RpgItems_Add(RpgItems *items, Vector2 position)
{
    if (items->count >= RPG_ITEM_MAX_COUNT) return false;
    RpgItem *item = &items->entries[items->count++];
    item->position = position; strcpy(item->name, "Item"); item->collected = false; return true;
}
int RpgItems_FindAtPosition(const RpgItems *items, Vector2 position, float distance)
{
    for (int index = 0; index < items->count; index++) if (fabsf(items->entries[index].position.x - position.x) <= distance && fabsf(items->entries[index].position.y - position.y) <= distance) return index;
    return -1;
}
bool RpgItems_RemoveAtPosition(RpgItems *items, Vector2 position, float distance)
{
    int index = RpgItems_FindAtPosition(items, position, distance);
    if (index < 0) return false;
    for (int next = index; next < items->count - 1; next++) items->entries[next] = items->entries[next + 1];
    items->count--;
    return true;
}
void RpgItems_Draw(const RpgItems *items)
{
    for (int index = 0; index < items->count; index++) if (!items->entries[index].collected)
        DrawPoly(items->entries[index].position, 5, 15.0f, -90.0f, GOLD);
}
// 役割: マップ上のアイテムと、消滅物から引き継いだ File.png ドロップを管理する。
