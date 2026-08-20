// 依存する自プロジェクト内ファイル: rpg_editor_drag.h
// 役割: クリックとドラッグを一貫した距離判定で分離し、カーソル追従座標を管理する。
#include "rpg_editor_drag.h"

#include <stddef.h>
#include <math.h>

#include "raymath.h"

enum { RPG_EDITOR_DRAG_START_DISTANCE = 5 };

void RpgEditorDrag_Begin(RpgEditorDrag *drag, Vector2 pointer)
{
    if (drag == NULL) return;
    drag->active = false;
    drag->pending = true;
    drag->pressPosition = pointer;
    drag->pointerPosition = pointer;
}

bool RpgEditorDrag_Update(RpgEditorDrag *drag, Vector2 pointer)
{
    if (drag == NULL || (!drag->pending && !drag->active)) return false;
    drag->pointerPosition = pointer;
    if (!drag->pending || Vector2Distance(pointer, drag->pressPosition) < RPG_EDITOR_DRAG_START_DISTANCE)
        return false;
    drag->pending = false;
    drag->active = true;
    return true;
}

bool RpgEditorDrag_IsBusy(const RpgEditorDrag *drag)
{ return drag != NULL && (drag->pending || drag->active); }

void RpgEditorDrag_End(RpgEditorDrag *drag)
{
    if (drag == NULL) return;
    drag->active = false;
    drag->pending = false;
}

Vector2 RpgEditorDrag_SnapToGrid(Vector2 position, float tileSize)
{
    if (tileSize <= 0.0f) return position;
    return (Vector2){ floorf(position.x / tileSize) * tileSize,
                      floorf(position.y / tileSize) * tileSize };
}
