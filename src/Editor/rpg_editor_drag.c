// 依存する自プロジェクト内ファイル: rpg_editor_drag.h
#include "rpg_editor_drag.h"

#include <stddef.h>
#include <math.h>

#include "raymath.h"

enum { RPG_EDITOR_DRAG_START_DISTANCE = 5 };

/* クリックとドラッグを一貫した距離判定で分離し、カーソル追従座標を管理する */

// クリック時のdrag初期化
void RpgEditorDrag_Begin(RpgEditorDrag *drag, Vector2 pointer)
{
    if (drag == NULL) return;

    drag->active = false;
    drag->pending = true;
    drag->pressPosition = pointer;
    drag->pointerPosition = pointer;
}

// ホールド時に閾値を超えた場合、ドラッグ開始と判定する
bool RpgEditorDrag_Update(RpgEditorDrag *drag, Vector2 pointer)
{
    if (drag == NULL || (!drag->pending && !drag->active)) return false;

    // マウス座標を取得するだけなら必要ない手順
    drag->pointerPosition = pointer;
    if (!drag->pending || Vector2Distance(pointer, drag->pressPosition) < RPG_EDITOR_DRAG_START_DISTANCE)
        return false;

    drag->pending = false;
    drag->active = true;
    return true;
}

// 既にドラッグが進行中かどうかを判定する
bool RpgEditorDrag_IsBusy(const RpgEditorDrag *drag)
{ return drag != NULL && (drag->pending || drag->active); }

// ドラッグ終了
void RpgEditorDrag_End(RpgEditorDrag *drag)
{
    if (drag == NULL) return;

    drag->active = false;
    drag->pending = false;
}

// グリッドにスナップした座標を取得（おそらくマウス座標のスナップ用）
Vector2 RpgEditorDrag_SnapToGrid(Vector2 position, float tileSize)
{
    if (tileSize <= 0.0f) return position;

    return (Vector2){ floorf(position.x / tileSize) * tileSize,
                      floorf(position.y / tileSize) * tileSize };
}

// ドラッグ中のマウス座標を中心とした矩形を取得する
Rectangle RpgEditorDrag_GetPreviewBounds(const RpgEditorDrag *drag, float width, float height)
{
    Vector2 pointer = drag != NULL ? drag->pointerPosition : (Vector2){ 0.0f, 0.0f };
    return (Rectangle){ pointer.x - width * 0.5f, pointer.y - height * 0.5f, width, height };
}
