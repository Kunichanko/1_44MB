// 依存する自プロジェクト内ファイル: なし
// 役割: エディター内のオブジェクト共通ドラッグの開始・追従・終了状態を提供する。
#ifndef RPG_EDITOR_DRAG_H
#define RPG_EDITOR_DRAG_H

#include "raylib.h"

typedef struct RpgEditorDrag {
    bool active;
    bool pending;
    Vector2 pressPosition;
    Vector2 pointerPosition;
} RpgEditorDrag;

void RpgEditorDrag_Begin(RpgEditorDrag *drag, Vector2 pointer);
bool RpgEditorDrag_Update(RpgEditorDrag *drag, Vector2 pointer);
bool RpgEditorDrag_IsBusy(const RpgEditorDrag *drag);
void RpgEditorDrag_End(RpgEditorDrag *drag);
Vector2 RpgEditorDrag_SnapToGrid(Vector2 position, float tileSize);

#endif
