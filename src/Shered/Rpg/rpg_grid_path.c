// 依存する自プロジェクト内ファイル: rpg_grid_path.h
#include "rpg_grid_path.h"

static bool RpgGridPath_AreSameCell(RpgGridCell first, RpgGridCell second)
{
    return first.row == second.row && first.column == second.column;
}

static int RpgGridPath_FindCell(const RpgGridPath *path, RpgGridCell cell)
{
    for (int index = 0; index < path->cellCount; index++)
        if (RpgGridPath_AreSameCell(path->cells[index], cell)) return index;
    return -1;
}

RpgGridPath RpgGridPath_Create(RpgGridCell start, RpgGridCell end)
{
    RpgGridPath path = { .cellCount = 2 };
    path.cells[0] = start;
    path.cells[1] = end;
    return path;
}

bool RpgGridPath_IsEndpoint(const RpgGridPath *path, RpgGridCell cell, bool *isStart)
{
    if (path->cellCount < 1) return false;
    if (RpgGridPath_AreSameCell(path->cells[0], cell)) {
        *isStart = true;
        return true;
    }
    if (RpgGridPath_AreSameCell(path->cells[path->cellCount - 1], cell)) {
        *isStart = false;
        return true;
    }
    return false;
}

bool RpgGridPath_MoveEndpoint(RpgGridPath *path, bool isStart, RpgGridCell destination,
                              int minimumCellCount)
{
    if (path->cellCount < 1 || minimumCellCount < 1) return false;
    int existingIndex = RpgGridPath_FindCell(path, destination);
    if (existingIndex >= 0) {
        // 経路上のセルに端点を重ねた場合は、反対側を保ったままその区間を短縮する。
        if (isStart && existingIndex > 0 && path->cellCount - existingIndex >= minimumCellCount) {
            for (int index = 0; index < path->cellCount - existingIndex; index++)
                path->cells[index] = path->cells[index + existingIndex];
            path->cellCount -= existingIndex;
            return true;
        }
        if (!isStart && existingIndex < path->cellCount - 1 && existingIndex + 1 >= minimumCellCount) {
            path->cellCount = existingIndex + 1;
            return true;
        }
        return false;
    }

    RpgGridCell endpoint = isStart ? path->cells[0] : path->cells[path->cellCount - 1];
    int distance = (endpoint.row - destination.row < 0 ? destination.row - endpoint.row : endpoint.row - destination.row) +
                   (endpoint.column - destination.column < 0 ? destination.column - endpoint.column : endpoint.column - destination.column);
    if (distance != 1 || path->cellCount >= RPG_GRID_PATH_MAX_CELLS) return false;
    // 受容体から作られた未延長経路は、最初の1マスから終点を伸ばし始める。
    if (path->cellCount == 1) {
        path->cells[1] = destination;
        path->cellCount = 2;
        return true;
    }
    // 隣接セルだけを追加するため、ドラッグでなぞった順序をそのまま経路として再利用できる。
    if (isStart) {
        for (int index = path->cellCount; index > 0; index--) path->cells[index] = path->cells[index - 1];
        path->cells[0] = destination;
    } else path->cells[path->cellCount] = destination;
    path->cellCount++;
    return true;
}

RpgGridCell RpgGridPath_GetSideNeighbor(RpgGridCell cell, RpgGridSide side)
{
    if (side == RPG_GRID_SIDE_TOP) cell.row--;
    else if (side == RPG_GRID_SIDE_RIGHT) cell.column++;
    else if (side == RPG_GRID_SIDE_BOTTOM) cell.row++;
    else cell.column--;
    return cell;
}
// 役割: 導線とデータ弾軌道で共用するグリッド経路の編集を提供する。
