// 依存する自プロジェクト内ファイル: rpg_item.h, rpg_stage.h
#include "rpg_item.h"
#include "rpg_stage.h"
#include "rpg_stage.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* The former star collectible has been retired.  Keep this tiny compatibility
 * module so existing stage packages containing rpg_items.cfg still load, but
 * never materialise, draw, collect, or save those legacy records. */
RpgItems RpgItems_Default(void) { return (RpgItems){ 0 }; }
bool RpgItems_Load(const char *filePath, RpgItems *items)
{
    FILE *file = fopen(filePath, "r");
    if (file == NULL) return false;
    /* Consume the legacy file only as a format-presence check.  Its entries
       deliberately do not enter the live stage. */
    items->count = 0;
    return fclose(file) == 0;
}
bool RpgItems_Save(const char *filePath, const RpgItems *items)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    (void)items;
    /* An ordinary stage save therefore clears any old star records. */
    fprintf(file, "0\n");
    return fclose(file) == 0;
}
bool RpgItems_Add(RpgItems *items, Vector2 position)
{
    (void)items;
    (void)position;
    return false;
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
    (void)items;
}
// 役割: マップ上のアイテムと、消滅物から引き継いだ File.png ドロップを管理する。
