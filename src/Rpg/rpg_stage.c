// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_stage.h
// 依存関係を更新: 特殊ブロックの種類を参照するため rpg_block_inventory.h を追加した。
#include "rpg_stage.h"

#include "raymath.h"

#include "rpg_block_inventory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

static void NormalizeReferencePath(const char *source, char *destination, size_t destinationSize)
{
    // 旧設定のCP932パスと、現行のUTF-8パスをどちらもUTF-8へそろえる。
    if (destinationSize == 0) return;
#ifdef _WIN32
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, NULL, 0) > 0) {
        snprintf(destination, destinationSize, "%s", source);
        return;
    }
    wchar_t widePath[RPG_STAGE_REFERENCE_PATH_LENGTH];
    if (MultiByteToWideChar(932, 0, source, -1, widePath,
                            RPG_STAGE_REFERENCE_PATH_LENGTH) > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, widePath, -1, destination, (int)destinationSize,
                            NULL, NULL) > 0) return;
#endif
    snprintf(destination, destinationSize, "%s", source);
}

// エリアスロットを初期化し、既存の配置・描画コードが使う横方向座標を保つ。
static void ClearMapSlot(RpgStage *stage, int mapIndex)
{
    if (mapIndex < 0 || mapIndex >= RPG_STAGE_MAP_COUNT) return;
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++)
        for (int column = 0; column < RPG_STAGE_COLUMNS; column++) {
            stage->blocks[row][firstColumn + column] = 0;
            stage->referencePaths[row][firstColumn + column][0] = '\0';
        }
}

static void FillMapGround(RpgStage *stage, int mapIndex)
{
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int column = firstColumn; column < firstColumn + RPG_STAGE_COLUMNS; column++) {
        stage->blocks[8][column] = 1;
        stage->blocks[9][column] = 1;
    }
}

static void GetDirectionOffset(RpgAreaDirection direction, int *x, int *y)
{
    *x = 0;
    *y = 0;
    if (direction == RPG_AREA_LEFT) *x = -1;
    else if (direction == RPG_AREA_RIGHT) *x = 1;
    else if (direction == RPG_AREA_UP) *y = -1;
    else if (direction == RPG_AREA_DOWN) *y = 1;
}

int RpgStage_GetMapCount(const RpgStage *stage)
{
    int count = 0;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) if (stage->mapActive[index]) count++;
    return count;
}

bool RpgStage_IsMapActive(const RpgStage *stage, int mapIndex)
{
    return stage != NULL && mapIndex >= 0 && mapIndex < RPG_STAGE_MAP_COUNT && stage->mapActive[mapIndex];
}

int RpgStage_GetMapAtGrid(const RpgStage *stage, int gridX, int gridY)
{
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
        if (stage->mapActive[index] && stage->mapGridX[index] == gridX && stage->mapGridY[index] == gridY)
            return index;
    return -1;
}

int RpgStage_FindNearestActiveMapAtGrid(const RpgStage *stage, int gridX, int gridY)
{
    if (stage == NULL) return -1;
    int nearestMap = -1;
    int nearestDistance = 0;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        if (!stage->mapActive[index]) continue;
        int distance = abs(stage->mapGridX[index] - gridX) + abs(stage->mapGridY[index] - gridY);
        if (nearestMap < 0 || distance < nearestDistance) {
            nearestMap = index;
            nearestDistance = distance;
        }
    }
    return nearestMap;
}

int RpgStage_FindNearestActiveMap(const RpgStage *stage, int mapIndex)
{
    if (RpgStage_IsMapActive(stage, mapIndex)) return mapIndex;
    int gridX = 0;
    int gridY = 0;
    if (stage != NULL && mapIndex >= 0 && mapIndex < RPG_STAGE_MAP_COUNT) {
        gridX = stage->mapGridX[mapIndex];
        gridY = stage->mapGridY[mapIndex];
    }
    return RpgStage_FindNearestActiveMapAtGrid(stage, gridX, gridY);
}

int RpgStage_GetAdjacentMap(const RpgStage *stage, int mapIndex, RpgAreaDirection direction)
{
    if (!RpgStage_IsMapActive(stage, mapIndex)) return -1;
    int offsetX;
    int offsetY;
    GetDirectionOffset(direction, &offsetX, &offsetY);
    return RpgStage_GetMapAtGrid(stage, stage->mapGridX[mapIndex] + offsetX,
                                 stage->mapGridY[mapIndex] + offsetY);
}

int RpgStage_GetOrCreateAdjacentMap(RpgStage *stage, int mapIndex, RpgAreaDirection direction)
{
    mapIndex = RpgStage_FindNearestActiveMap(stage, mapIndex);
    if (mapIndex < 0) return -1;
    int adjacent = RpgStage_GetAdjacentMap(stage, mapIndex, direction);
    if (adjacent >= 0) return adjacent;
    if (!RpgStage_IsMapActive(stage, mapIndex)) return -1;
    int freeIndex = -1;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
        if (!stage->mapActive[index]) { freeIndex = index; break; }
    if (freeIndex < 0) return -1;
    int offsetX;
    int offsetY;
    GetDirectionOffset(direction, &offsetX, &offsetY);
    ClearMapSlot(stage, freeIndex);
    FillMapGround(stage, freeIndex);
    stage->mapActive[freeIndex] = true;
    stage->mapGridX[freeIndex] = stage->mapGridX[mapIndex] + offsetX;
    stage->mapGridY[freeIndex] = stage->mapGridY[mapIndex] + offsetY;
    return freeIndex;
}

bool RpgStage_RemoveMap(RpgStage *stage, int mapIndex)
{
    if (!RpgStage_IsMapActive(stage, mapIndex) || RpgStage_GetMapCount(stage) <= 1) return false;
    ClearMapSlot(stage, mapIndex);
    stage->mapActive[mapIndex] = false;
    // 再作成前の二次元IDを残し、Revertなどで無効なスロットを参照した際の最寄り判定に使う。
    return true;
}

RpgStage RpgStage_Default(void)
{
    RpgStage stage = {0};
    for (int mapIndex = 0; mapIndex < RPG_STAGE_INITIAL_MAP_COUNT; mapIndex++) {
        stage.mapActive[mapIndex] = true;
        stage.mapGridX[mapIndex] = mapIndex;
        FillMapGround(&stage, mapIndex);
    }
    return stage;
}

bool RpgStage_Load(const char *filePath, RpgStage *stage)
{
    FILE *file = fopen(filePath, "r");
    char line[2048];
    bool loadedAny = false;
    if (file == NULL) return false;
    *stage = RpgStage_Default();
    memset(stage->referencePaths, 0, sizeof(stage->referencePaths));
    // 旧3ステージ形式は行ごとに読み、存在しない4〜6ステージを初期地面のまま残す。
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        if (fgets(line, sizeof(line), file) == NULL) break;
        char *cursor = line;
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
            char *next = NULL;
            long value = strtol(cursor, &next, 10);
            if (next == cursor) break;
            stage->blocks[row][column] = (int)value;
            cursor = next;
            loadedAny = true;
        }
    }
    // 旧形式のグリッド読み込み後に、任意個の参照オブジェクト設定を追加で復元する。
    while (fgets(line, sizeof(line), file) != NULL) {
        int row;
        int column;
        char path[RPG_STAGE_REFERENCE_PATH_LENGTH];
        if (sscanf(line, "reference %d %d %259[^\n]", &row, &column, path) == 3 &&
            row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
            stage->blocks[row][column] == RPG_BLOCK_REFERENCE_FILE) {
            NormalizeReferencePath(path, stage->referencePaths[row][column],
                                   RPG_STAGE_REFERENCE_PATH_LENGTH);
        } else if (strncmp(line, "areas_begin", 11) == 0) {
            memset(stage->mapActive, 0, sizeof(stage->mapActive));
            memset(stage->mapGridX, 0, sizeof(stage->mapGridX));
            memset(stage->mapGridY, 0, sizeof(stage->mapGridY));
        } else {
            int mapIndex;
            int gridX;
            int gridY;
            if (sscanf(line, "area %d %d %d", &mapIndex, &gridX, &gridY) == 3 &&
                mapIndex >= 0 && mapIndex < RPG_STAGE_MAP_COUNT) {
                stage->mapActive[mapIndex] = true;
                stage->mapGridX[mapIndex] = gridX;
                stage->mapGridY[mapIndex] = gridY;
            }
        }
    }
    fclose(file); return loadedAny;
}

bool RpgStage_Save(const char *filePath, const RpgStage *stage)
{
    FILE *file = fopen(filePath, "w");
    if (file == NULL) return false;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) fprintf(file, "%d ", stage->blocks[row][column]);
        fputc('\n', file);
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        if (stage->blocks[row][column] == RPG_BLOCK_REFERENCE_FILE && stage->referencePaths[row][column][0] != '\0')
            fprintf(file, "reference %d %d %s\n", row, column, stage->referencePaths[row][column]);
    fputs("areas_begin\n", file);
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++)
        if (stage->mapActive[mapIndex]) fprintf(file, "area %d %d %d\n", mapIndex,
                                                stage->mapGridX[mapIndex], stage->mapGridY[mapIndex]);
    return fclose(file) == 0;
}

bool RpgStage_SetBlockAtPosition(RpgStage *stage, Vector2 position, bool isBlock)
{
    int column = (int)(position.x / RPG_STAGE_TILE_SIZE);
    int row = (int)(position.y / RPG_STAGE_TILE_SIZE);
    if (column < 0 || column >= RPG_STAGE_WORLD_COLUMNS || row < 0 || row >= RPG_STAGE_ROWS) return false;
    stage->blocks[row][column] = isBlock ? 1 : 0;
    stage->referencePaths[row][column][0] = '\0';
    return true;
}

bool RpgStage_SetBlockTypeAtPosition(RpgStage *stage, Vector2 position, int blockType)
{
    int column = (int)(position.x / RPG_STAGE_TILE_SIZE);
    int row = (int)(position.y / RPG_STAGE_TILE_SIZE);
    if (column < 0 || column >= RPG_STAGE_WORLD_COLUMNS || row < 0 || row >= RPG_STAGE_ROWS) return false;
    stage->blocks[row][column] = blockType;
    if (blockType != RPG_BLOCK_REFERENCE_FILE) stage->referencePaths[row][column][0] = '\0';
    return true;
}

int RpgStage_GetBlockTypeAtPosition(const RpgStage *stage, Vector2 position)
{
    int column = (int)(position.x / RPG_STAGE_TILE_SIZE);
    int row = (int)(position.y / RPG_STAGE_TILE_SIZE);
    if (column < 0 || column >= RPG_STAGE_WORLD_COLUMNS || row < 0 || row >= RPG_STAGE_ROWS) return 0;
    return stage->blocks[row][column];
}

bool RpgStage_SetDoorOpenAtCell(RpgStage *stage, int row, int column, bool isOpen)
{
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
        column >= RPG_STAGE_WORLD_COLUMNS || !RpgBlockInventory_IsDoorBlock(stage->blocks[row][column]))
        return false;
    const RpgEffectShape *currentShape = RpgBlockInventory_GetEffectShape(stage->blocks[row][column]);
    const RpgEffectShape *targetShape = RpgBlockInventory_GetDoorShape(isOpen);
    if (currentShape == NULL || targetShape == NULL || currentShape->cellCount != targetShape->cellCount ||
        RpgBlockInventory_IsDoorOpen(currentShape->rootType) == isOpen) return false;
    for (int cellIndex = 0; cellIndex < currentShape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &currentShape->cells[cellIndex];
        if (cell->blockType != stage->blocks[row][column]) continue;
        int rootRow = row - cell->offsetY;
        int rootColumn = column - cell->offsetX;
        bool hasWholeDoor = rootRow >= 0 && rootColumn >= 0;
        for (int partIndex = 0; hasWholeDoor && partIndex < currentShape->cellCount; partIndex++) {
            const RpgEffectShapeCell *part = &currentShape->cells[partIndex];
            int partRow = rootRow + part->offsetY;
            int partColumn = rootColumn + part->offsetX;
            hasWholeDoor = partRow >= 0 && partRow < RPG_STAGE_ROWS &&
                           partColumn >= 0 && partColumn < RPG_STAGE_WORLD_COLUMNS &&
                           stage->blocks[partRow][partColumn] == part->blockType;
        }
        if (!hasWholeDoor) continue;
        // 閉じたドアの全構成マスを、同じ形状の開状態へまとめて差し替える。
        for (int partIndex = 0; partIndex < targetShape->cellCount; partIndex++) {
            const RpgEffectShapeCell *part = &targetShape->cells[partIndex];
            stage->blocks[rootRow + part->offsetY][rootColumn + part->offsetX] = part->blockType;
        }
        return true;
    }
    return false;
}

bool RpgStage_SetReferencePathAtCell(RpgStage *stage, int row, int column, const char *path)
{
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE || path == NULL) return false;
    snprintf(stage->referencePaths[row][column], RPG_STAGE_REFERENCE_PATH_LENGTH, "%s", path);
    return true;
}

const char *RpgStage_GetReferencePathAtCell(const RpgStage *stage, int row, int column)
{
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) return "";
    return stage->referencePaths[row][column];
}

RpgReferenceObjects RpgReferenceObjects_Default(void) { return (RpgReferenceObjects){ 0 }; }

bool RpgReferenceObjects_AddDrop(RpgReferenceObjects *objects, Vector2 position, const char *path)
{
    if (objects == NULL || path == NULL || path[0] == '\0' || objects->count >= RPG_REFERENCE_OBJECT_MAX_COUNT) return false;
    RpgReferenceObject *object = &objects->entries[objects->count++];
    *object = (RpgReferenceObject){ .position = position, .isFalling = true };
    snprintf(object->path, sizeof(object->path), "%s", path);
    return true;
}

void RpgReferenceObjects_Update(RpgReferenceObjects *objects, float deltaTime)
{
    for (int index = 0; index < objects->count; index++) {
        RpgReferenceObject *object = &objects->entries[index];
        if (!object->isFalling) continue;
        object->fallSpeed += 720.0f * deltaTime;
        object->position.y += object->fallSpeed * deltaTime;
        if (object->position.y >= 382.0f) {
            object->position.y = 382.0f;
            object->fallSpeed = 0.0f;
            object->isFalling = false;
        }
    }
}

void RpgReferenceObjects_Draw(const RpgReferenceObjects *objects, Texture2D fileTexture)
{
    RpgReferenceObjects_DrawExcept(objects, fileTexture, -1);
}

void RpgReferenceObjects_DrawExcept(const RpgReferenceObjects *objects, Texture2D fileTexture,
                                    int excludedIndex)
{
    for (int index = 0; index < objects->count; index++) {
        if (index == excludedIndex) continue;
        const RpgReferenceObject *object = &objects->entries[index];
        RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ object->position.x - 24.0f,
                                      object->position.y - 24.0f, 48.0f, 48.0f }, WHITE);
    }
}

int RpgReferenceObjects_FindNearby(const RpgReferenceObjects *objects, Vector2 position, float distance)
{
    int closestIndex = -1;
    float closestDistance = distance;
    for (int index = 0; index < objects->count; index++) {
        float objectDistance = Vector2Distance(position, objects->entries[index].position);
        if (objectDistance <= closestDistance) { closestDistance = objectDistance; closestIndex = index; }
    }
    return closestIndex;
}

bool RpgReferenceObjects_FindNearbyTarget(const RpgStage *stage, const RpgReferenceObjects *objects,
                                          Vector2 position, float distance, RpgReferenceTarget *target)
{
    if (target == NULL) return false;
    *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 };
    float closestDistance = distance;
    for (int index = 0; index < objects->count; index++) {
        float objectDistance = Vector2Distance(position, objects->entries[index].position);
        if (objectDistance <= closestDistance) {
            closestDistance = objectDistance;
            *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_DROP, .row = -1,
                                             .column = -1, .dropIndex = index };
        }
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) continue;
        Vector2 objectPosition = { (column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                   (row + 0.5f) * RPG_STAGE_TILE_SIZE };
        float objectDistance = Vector2Distance(position, objectPosition);
        if (objectDistance <= closestDistance) {
            closestDistance = objectDistance;
            *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_CELL, .row = row,
                                             .column = column, .dropIndex = -1 };
        }
    }
    return target->kind != RPG_REFERENCE_TARGET_NONE;
}

bool RpgReferenceObjects_FindTarget(const RpgStage *stage, const RpgReferenceObjects *objects,
                                    Vector2 position, RpgReferenceTarget *target)
{
    if (target == NULL) return false;
    *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 };
    for (int index = objects->count - 1; index >= 0; index--) {
        const RpgReferenceObject *object = &objects->entries[index];
        if (CheckCollisionPointRec(position, (Rectangle){ object->position.x - 24.0f, object->position.y - 24.0f, 48.0f, 48.0f })) {
            target->kind = RPG_REFERENCE_TARGET_DROP;
            target->dropIndex = index;
            return true;
        }
    }
    int column = (int)(position.x / RPG_STAGE_TILE_SIZE);
    int row = (int)(position.y / RPG_STAGE_TILE_SIZE);
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        stage->blocks[row][column] != RPG_BLOCK_REFERENCE_FILE) return false;
    target->kind = RPG_REFERENCE_TARGET_CELL;
    target->row = row;
    target->column = column;
    return true;
}

const char *RpgReferenceObjects_GetTargetPath(const RpgStage *stage, const RpgReferenceObjects *objects,
                                               RpgReferenceTarget target)
{
    if (target.kind == RPG_REFERENCE_TARGET_CELL) return RpgStage_GetReferencePathAtCell(stage, target.row, target.column);
    if (target.kind == RPG_REFERENCE_TARGET_DROP && target.dropIndex >= 0 && target.dropIndex < objects->count)
        return objects->entries[target.dropIndex].path;
    return "";
}

bool RpgReferenceObjects_RemoveTarget(RpgStage *stage, RpgReferenceObjects *objects,
                                      RpgReferenceTarget target)
{
    if (target.kind == RPG_REFERENCE_TARGET_CELL) {
        if (target.row < 0 || target.row >= RPG_STAGE_ROWS || target.column < 0 || target.column >= RPG_STAGE_WORLD_COLUMNS) return false;
        stage->blocks[target.row][target.column] = 0;
        stage->referencePaths[target.row][target.column][0] = '\0';
        return true;
    }
    if (target.kind != RPG_REFERENCE_TARGET_DROP || target.dropIndex < 0 || target.dropIndex >= objects->count) return false;
    for (int index = target.dropIndex; index < objects->count - 1; index++) objects->entries[index] = objects->entries[index + 1];
    objects->count--;
    return true;
}

bool RpgStage_IsSolidBlock(int blockType)
{
    // 開いたドアは下側2マスだけ通行可能とし、上側1マスは扉の枠として壁に残す。
    if (blockType == RPG_BLOCK_DOOR_OPEN_MIDDLE || blockType == RPG_BLOCK_DOOR_OPEN_BOTTOM) return false;
    if (RpgBlockInventory_IsReferenceObject(blockType)) return false;
    return blockType != 0;
}

static bool RpgStage_CheckBlockCollision(Rectangle bounds, Rectangle cell, int blockType)
{
    // 穴付きブロックは、半マス幅の穴以外だけを実体のある壁として扱う。
    if (blockType == RPG_BLOCK_HOLE_VERTICAL) {
        float sideWidth = RPG_STAGE_TILE_SIZE * 0.25f;
        return CheckCollisionRecs(bounds, (Rectangle){ cell.x, cell.y, sideWidth, cell.height }) ||
               CheckCollisionRecs(bounds, (Rectangle){ cell.x + cell.width - sideWidth, cell.y, sideWidth, cell.height });
    }
    if (blockType == RPG_BLOCK_HOLE_HORIZONTAL) {
        float sideHeight = RPG_STAGE_TILE_SIZE * 0.25f;
        return CheckCollisionRecs(bounds, (Rectangle){ cell.x, cell.y, cell.width, sideHeight }) ||
               CheckCollisionRecs(bounds, (Rectangle){ cell.x, cell.y + cell.height - sideHeight, cell.width, sideHeight });
    }
    return RpgStage_IsSolidBlock(blockType) && CheckCollisionRecs(bounds, cell);
}

static int RpgStage_ClampIndex(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

bool RpgStage_CheckSolidCollision(const RpgStage *stage, Rectangle bounds)
{
    return RpgStage_FindSolidCollisionCenter(stage, bounds, NULL);
}

bool RpgStage_FindSolidCollisionCenter(const RpgStage *stage, Rectangle bounds, Vector2 *center)
{
    // 複数マスの特殊ブロックも構成マスごとに走査し、実際に衝突した1マスの中心を返す。
    int firstColumn = (int)floorf(bounds.x / RPG_STAGE_TILE_SIZE);
    int lastColumn = (int)floorf((bounds.x + bounds.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    int firstRow = (int)floorf(bounds.y / RPG_STAGE_TILE_SIZE);
    int lastRow = (int)floorf((bounds.y + bounds.height - 0.001f) / RPG_STAGE_TILE_SIZE);
    if (lastColumn < 0 || firstColumn >= RPG_STAGE_WORLD_COLUMNS ||
        lastRow < 0 || firstRow >= RPG_STAGE_ROWS) return false;
    firstColumn = RpgStage_ClampIndex(firstColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    lastColumn = RpgStage_ClampIndex(lastColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    firstRow = RpgStage_ClampIndex(firstRow, 0, RPG_STAGE_ROWS - 1);
    lastRow = RpgStage_ClampIndex(lastRow, 0, RPG_STAGE_ROWS - 1);
    for (int row = firstRow; row <= lastRow; row++) {
        for (int column = firstColumn; column <= lastColumn; column++) {
            Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                               RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
            if (RpgStage_CheckBlockCollision(bounds, cell, stage->blocks[row][column])) {
                if (center != NULL)
                    *center = (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f };
                return true;
            }
        }
    }
    return false;
}

bool RpgStage_CheckSolidCircleCollision(const RpgStage *stage, Vector2 center, float radius)
{
    return RpgStage_FindSolidCircleCollisionCenter(stage, center, radius, NULL);
}

bool RpgStage_FindSolidCircleCollisionCenter(const RpgStage *stage, Vector2 center, float radius,
                                             Vector2 *collisionCenter)
{
    // 弾の半径まで含めて走査し、穴付きブロックも実際の壁部分だけと円で判定する。
    Rectangle range = { center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f };
    int firstColumn = (int)floorf(range.x / RPG_STAGE_TILE_SIZE);
    int lastColumn = (int)floorf((range.x + range.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    int firstRow = (int)floorf(range.y / RPG_STAGE_TILE_SIZE);
    int lastRow = (int)floorf((range.y + range.height - 0.001f) / RPG_STAGE_TILE_SIZE);
    if (lastColumn < 0 || firstColumn >= RPG_STAGE_WORLD_COLUMNS ||
        lastRow < 0 || firstRow >= RPG_STAGE_ROWS) return false;
    firstColumn = RpgStage_ClampIndex(firstColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    lastColumn = RpgStage_ClampIndex(lastColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    firstRow = RpgStage_ClampIndex(firstRow, 0, RPG_STAGE_ROWS - 1);
    lastRow = RpgStage_ClampIndex(lastRow, 0, RPG_STAGE_ROWS - 1);
    for (int row = firstRow; row <= lastRow; row++) for (int column = firstColumn; column <= lastColumn; column++) {
        int blockType = stage->blocks[row][column];
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        if (blockType == RPG_BLOCK_HOLE_VERTICAL) {
            float sideWidth = RPG_STAGE_TILE_SIZE * 0.25f;
            if (CheckCollisionCircleRec(center, radius, (Rectangle){ cell.x, cell.y, sideWidth, cell.height }) ||
                CheckCollisionCircleRec(center, radius, (Rectangle){ cell.x + cell.width - sideWidth, cell.y, sideWidth, cell.height })) {
                if (collisionCenter != NULL)
                    *collisionCenter = (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f };
                return true;
            }
        } else if (blockType == RPG_BLOCK_HOLE_HORIZONTAL) {
            float sideHeight = RPG_STAGE_TILE_SIZE * 0.25f;
            if (CheckCollisionCircleRec(center, radius, (Rectangle){ cell.x, cell.y, cell.width, sideHeight }) ||
                CheckCollisionCircleRec(center, radius, (Rectangle){ cell.x, cell.y + cell.height - sideHeight, cell.width, sideHeight })) {
                if (collisionCenter != NULL)
                    *collisionCenter = (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f };
                return true;
            }
        } else if (RpgStage_IsSolidBlock(blockType) && CheckCollisionCircleRec(center, radius, cell)) {
            if (collisionCenter != NULL)
                *collisionCenter = (Vector2){ cell.x + cell.width * 0.5f, cell.y + cell.height * 0.5f };
            return true;
        }
    }
    return false;
}

bool RpgStage_IsSolidAtPosition(const RpgStage *stage, Vector2 position)
{
    Rectangle probe = { position.x, position.y, 0.01f, 0.01f };
    return RpgStage_CheckSolidCollision(stage, probe);
}

Color RpgStage_GetBlockColor(int blockType)
{
    static const Color colors[] = { { 0 }, { 116, 78, 48, 255 }, { 91, 130, 66, 255 },
                                    { 75, 104, 156, 255 }, { 154, 99, 64, 255 }, { 116, 72, 130, 255 },
                                    { 82, 192, 210, 255 }, { 225, 91, 140, 255 }, { 232, 197, 76, 255 },
                                    { 103, 220, 178, 255 }, { 176, 104, 226, 255 },
                                    { 244, 153, 64, 255 }, { 88, 126, 220, 255 },
                                    { 235, 122, 72, 255 }, { 235, 122, 72, 255 },
                                    { 172, 90, 222, 255 }, { 172, 90, 222, 255 }, { 172, 90, 222, 255 },
                                    { 94, 60, 40, 255 }, { 94, 60, 40, 255 }, { 94, 60, 40, 255 },
                                    { 126, 90, 58, 180 }, { 126, 90, 58, 180 }, { 126, 90, 58, 180 } };
    // 開いたドアの上端は残る壁として、閉じたドアと同じ不透明な色で描画する。
    if (blockType == RPG_BLOCK_DOOR_OPEN_TOP) blockType = RPG_BLOCK_DOOR_CLOSED_TOP;
    return blockType >= 1 && blockType <= RPG_BLOCK_DOOR_OPEN_BOTTOM ? colors[blockType] :
           blockType == RPG_BLOCK_EFFECT_BUTTON ? (Color){ 72, 84, 104, 255 } : colors[1];
}

static void DrawEffectSymbol(Rectangle cell, int blockType)
{
    if (blockType == RPG_BLOCK_EFFECT_BOUNCE) DrawCircle((int)(cell.x + 24.0f), (int)(cell.y + 24.0f), 12.0f, RAYWHITE);
    if (blockType == RPG_BLOCK_EFFECT_SLOW) DrawTriangle((Vector2){ cell.x + 13.0f, cell.y + 32.0f },
                                                         (Vector2){ cell.x + 35.0f, cell.y + 24.0f },
                                                         (Vector2){ cell.x + 13.0f, cell.y + 16.0f }, RAYWHITE);
    if (blockType == RPG_BLOCK_EFFECT_WIDE_BOUNCE) {
        DrawRectangleRec((Rectangle){ cell.x + 8.0f, cell.y + 17.0f, RPG_STAGE_TILE_SIZE + 32.0f, 14.0f }, RAYWHITE);
        DrawCircle((int)(cell.x + 24.0f), (int)(cell.y + 24.0f), 9.0f, ORANGE);
        DrawCircle((int)(cell.x + 56.0f), (int)(cell.y + 24.0f), 9.0f, ORANGE);
    }
    if (blockType == RPG_BLOCK_EFFECT_CORNER_BOUNCE) {
        DrawLineEx((Vector2){ cell.x + 12.0f, cell.y + 15.0f }, (Vector2){ cell.x + 58.0f, cell.y + 15.0f }, 5.0f, RAYWHITE);
        DrawLineEx((Vector2){ cell.x + 12.0f, cell.y + 15.0f }, (Vector2){ cell.x + 12.0f, cell.y + 58.0f }, 5.0f, RAYWHITE);
    }
    if (blockType == RPG_BLOCK_DOOR_CLOSED_TOP) {
        DrawRectangleLinesEx((Rectangle){ cell.x + 8.0f, cell.y + 7.0f, 32.0f, RPG_STAGE_TILE_SIZE * 3.0f - 14.0f }, 3.0f, GOLD);
    }
    if (blockType == RPG_BLOCK_DOOR_OPEN_TOP) {
        DrawCircleV((Vector2){ cell.x + 24.0f, cell.y + 12.0f }, 14.0f, Fade(YELLOW, 0.35f));
        DrawCircleV((Vector2){ cell.x + 24.0f, cell.y + 12.0f }, 7.0f, YELLOW);
        DrawLineEx((Vector2){ cell.x + 8.0f, cell.y + 12.0f }, (Vector2){ cell.x + 40.0f, cell.y + 12.0f }, 2.0f, RAYWHITE);
    }
    if (blockType == RPG_BLOCK_EFFECT_BUTTON) {
        DrawRectangleRec((Rectangle){ cell.x + 7.0f, cell.y + 29.0f, 34.0f, 10.0f }, DARKGRAY);
        DrawRectangleLinesEx((Rectangle){ cell.x + 7.0f, cell.y + 29.0f, 34.0f, 10.0f }, 2.0f, RAYWHITE);
        DrawCircle((int)cell.x + 24, (int)cell.y + 28, 7.0f, RED);
    }
    // 伸縮後にも残る根元マスを、紫のアンカー記号で明確に示す。
    if (blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL ||
        blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL ||
        blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT ||
        blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP) {
        DrawCircle((int)cell.x + 24, (int)cell.y + 24, 12.0f, Fade(PURPLE, 0.55f));
        DrawCircleLines((int)cell.x + 24, (int)cell.y + 24, 12.0f, RAYWHITE);
        Vector2 end = blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL ?
            (Vector2){ cell.x + 24.0f, cell.y + 39.0f } :
            blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT ? (Vector2){ cell.x + 9.0f, cell.y + 24.0f } :
            blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP ? (Vector2){ cell.x + 24.0f, cell.y + 9.0f } :
            (Vector2){ cell.x + 39.0f, cell.y + 24.0f };
        DrawLineEx((Vector2){ cell.x + 24.0f, cell.y + 24.0f }, end, 3.0f, GOLD);
    }
}

static void DrawBlockCell(Rectangle cell, int blockType)
{
    Color color = RpgStage_GetBlockColor(blockType);
    // 半マス幅の穴を塗らずに残し、見た目と当たり判定の通路を一致させる。
    if (blockType == RPG_BLOCK_HOLE_VERTICAL) {
        DrawRectangleRec((Rectangle){ cell.x, cell.y, cell.width * 0.25f, cell.height }, color);
        DrawRectangleRec((Rectangle){ cell.x + cell.width * 0.75f, cell.y, cell.width * 0.25f, cell.height }, color);
    } else if (blockType == RPG_BLOCK_HOLE_HORIZONTAL) {
        DrawRectangleRec((Rectangle){ cell.x, cell.y, cell.width, cell.height * 0.25f }, color);
        DrawRectangleRec((Rectangle){ cell.x, cell.y + cell.height * 0.75f, cell.width, cell.height * 0.25f }, color);
    } else if (RpgStage_IsSolidBlock(blockType)) {
        DrawRectangleRec(cell, color);
    }
}

void RpgStage_Draw(const RpgStage *stage, bool showGrid)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        int blockType = stage->blocks[row][column];
        // 開いたドアの下2マスは空白、穴付きブロックは穴の外側だけを描画する。
        DrawBlockCell(cell, blockType);
        if (showGrid) DrawRectangleLinesEx(cell, 1.0f, Fade(DARKGRAY, 0.45f));
    }
}

void RpgStage_DrawMap(const RpgStage *stage, int mapIndex, bool showGrid)
{
    int startColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        int blockType = stage->blocks[row][startColumn + column];
        // マップ表示もゲーム本編と同じ壁形状を描画する。
        DrawBlockCell(cell, blockType);
        if (showGrid) DrawRectangleLinesEx(cell, 1.0f, Fade(DARKGRAY, 0.45f));
    }
}

void RpgStage_DrawReferenceObject(Texture2D fileTexture, Rectangle cell, Color tint)
{
    if (fileTexture.id == 0) return;
    // 参照オブジェクトは1マスに収め、壁や地面の見た目・当たり判定を持たせない。
    DrawTexturePro(fileTexture, (Rectangle){ 0.0f, 0.0f, (float)fileTexture.width, (float)fileTexture.height },
                   (Rectangle){ cell.x + 4.0f, cell.y + 4.0f, cell.width - 8.0f, cell.height - 8.0f },
                   (Vector2){ 0.0f, 0.0f }, 0.0f, tint);
}

void RpgStage_DrawReferenceObjects(const RpgStage *stage, Texture2D fileTexture)
{
    RpgStage_DrawReferenceObjectsExcept(stage, fileTexture, -1, -1);
}

void RpgStage_DrawReferenceObjectsExcept(const RpgStage *stage, Texture2D fileTexture,
                                         int excludedRow, int excludedColumn)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        if (row != excludedRow || column != excludedColumn)
        if (stage->blocks[row][column] == RPG_BLOCK_REFERENCE_FILE)
            RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                                                     RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }, WHITE);
}

void RpgStage_DrawMapReferenceObjects(const RpgStage *stage, int mapIndex, Texture2D fileTexture)
{
    int startColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_COLUMNS; column++)
        if (stage->blocks[row][startColumn + column] == RPG_BLOCK_REFERENCE_FILE)
            RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                                                     RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }, WHITE);
}

// 地面や前景を描いた後にも特殊効果の記号だけを前面へ出せるようにする。
void RpgStage_DrawEffects(const RpgStage *stage)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        DrawEffectSymbol(cell, stage->blocks[row][column]);
    }
}

void RpgStage_DrawMapEffects(const RpgStage *stage, int mapIndex)
{
    int startColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        DrawEffectSymbol(cell, stage->blocks[row][startColumn + column]);
    }
}
// 役割: RPG ステージのグリッド、地形判定、特殊ブロック、描画を管理する。
