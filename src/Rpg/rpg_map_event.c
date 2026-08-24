// 依存する自プロジェクト内ファイル: rpg_map_event.h
#include "rpg_map_event.h"
// 依存関係を更新: 表示ピクセルの共通スナップ処理を使うため rpg_stage.h を追加する。
#include "rpg_stage.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
RpgMapEvents RpgMapEvents_Default(void) { return (RpgMapEvents){0}; }
Vector2 RpgMapEvents_SnapToCell(Vector2 position)
{
    float tileSize = (float)RPG_STAGE_TILE_SIZE;
    return (Vector2){ floorf(position.x / tileSize) * tileSize + tileSize * 0.5f,
                      floorf(position.y / tileSize) * tileSize + tileSize * 0.5f };
}
bool RpgMapEvents_Load(const char *path, RpgMapEvents *events) { FILE *f=fopen(path,"r"); if(!f)return false; if(fscanf(f,"%d",&events->count)!=1||events->count<0||events->count>RPG_MAP_EVENT_MAX_COUNT){fclose(f);return false;} for(int i=0;i<events->count;i++){if(fscanf(f,"%f %f %63s",&events->entries[i].position.x,&events->entries[i].position.y,events->entries[i].name)!=3){fclose(f);return false;}events->entries[i].position=RpgMapEvents_SnapToCell(events->entries[i].position);events->entries[i].triggered=false;} return fclose(f)==0; }
bool RpgMapEvents_Save(const char *path,const RpgMapEvents *events){FILE*f=fopen(path,"w");if(!f)return false;fprintf(f,"%d\n",events->count);for(int i=0;i<events->count;i++)fprintf(f,"%.1f %.1f %s\n",events->entries[i].position.x,events->entries[i].position.y,events->entries[i].name);return fclose(f)==0;}
bool RpgMapEvents_Add(RpgMapEvents *events,Vector2 position){
    position=RpgMapEvents_SnapToCell(position);
    if(events->count>=RPG_MAP_EVENT_MAX_COUNT)return false;
    for(int i=0;i<events->count;i++)
        if(events->entries[i].position.x==position.x&&events->entries[i].position.y==position.y)return false;
    RpgMapEvent*e=&events->entries[events->count++];e->position=position;strcpy(e->name,"Event");e->triggered=false;return true;
}
bool RpgMapEvents_Move(RpgMapEvents *events, int index, Vector2 position)
{
    if (events == NULL || index < 0 || index >= events->count) return false;
    position = RpgMapEvents_SnapToCell(position);
    for (int other = 0; other < events->count; other++)
        if (other != index && events->entries[other].position.x == position.x &&
            events->entries[other].position.y == position.y) return false;
    events->entries[index].position = position;
    return true;
}
int RpgMapEvents_FindAtPosition(const RpgMapEvents *events,Vector2 position,float distance){for(int i=0;i<events->count;i++)if(fabsf(events->entries[i].position.x-position.x)<=distance&&fabsf(events->entries[i].position.y-position.y)<=distance)return i;return -1;}
bool RpgMapEvents_RemoveAtPosition(RpgMapEvents *events,Vector2 position,float distance){int i=RpgMapEvents_FindAtPosition(events,position,distance);if(i<0)return false;for(;i<events->count-1;i++)events->entries[i]=events->entries[i+1];events->count--;return true;}
void RpgMapEvents_Draw(const RpgMapEvents *events){for(int i=0;i<events->count;i++){Vector2 p=RpgStage_SnapRenderPoint(events->entries[i].position);DrawCircleLines((int)p.x,(int)p.y,(int)RpgStage_SnapRenderCoordinate(11.0f),ORANGE);}}
// 役割: 指定位置へ到達した時に起動するマップイベントを管理する。
