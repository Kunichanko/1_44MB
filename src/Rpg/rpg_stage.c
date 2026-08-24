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

float RpgStage_SnapRenderCoordinate(float coordinate)
{
    const float displayScale = 960.0f / (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
    return roundf(coordinate * displayScale) / displayScale;
}

Vector2 RpgStage_SnapRenderPoint(Vector2 point)
{
    return (Vector2){ RpgStage_SnapRenderCoordinate(point.x),
                      RpgStage_SnapRenderCoordinate(point.y) };
}

Rectangle RpgStage_SnapRenderRectangle(Rectangle rectangle)
{
    return (Rectangle){ RpgStage_SnapRenderCoordinate(rectangle.x),
                        RpgStage_SnapRenderCoordinate(rectangle.y),
                        RpgStage_SnapRenderCoordinate(rectangle.width),
                        RpgStage_SnapRenderCoordinate(rectangle.height) };
}

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
    for (int index = stage->imageObjects.count - 1; index >= 0; index--) {
        RpgImageObject *object = &stage->imageObjects.entries[index];
        if (object->column < firstColumn || object->column >= firstColumn + RPG_STAGE_COLUMNS) continue;
        int remaining = stage->imageObjects.count - index - 1;
        if (remaining > 0) memmove(object, object + 1, (size_t)remaining * sizeof(*object));
        stage->imageObjects.count--;
    }
}

static void FillMapGround(RpgStage *stage, int mapIndex)
{
    int firstColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int column = firstColumn; column < firstColumn + RPG_STAGE_COLUMNS; column++) {
        stage->blocks[RPG_STAGE_GROUND_START_ROW][column] = 1;
        stage->blocks[RPG_STAGE_GROUND_START_ROW + 1][column] = 1;
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
    stage.imageObjects = RpgImageObjects_Default();
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
    int savedTileSize = 0, savedColumns = 0, savedRows = 0;
    bool isCurrentGrid = fgets(line, sizeof(line), file) != NULL &&
                         sscanf(line, "grid %d %d %d", &savedTileSize, &savedColumns, &savedRows) == 3 &&
                         savedTileSize == RPG_STAGE_TILE_SIZE && savedColumns == RPG_STAGE_COLUMNS &&
                         savedRows == RPG_STAGE_ROWS;
    if (!isCurrentGrid) {
        /* 旧48px・20x10形式はマスの対応を保てないため、地面だけの新グリッドへ安全に初期化する。 */
        bool readingAreas = false;
        rewind(file);
        while (fgets(line, sizeof(line), file) != NULL) {
            if (strncmp(line, "areas_begin", 11) == 0) {
                memset(stage->mapActive, 0, sizeof(stage->mapActive));
                memset(stage->mapGridX, 0, sizeof(stage->mapGridX));
                memset(stage->mapGridY, 0, sizeof(stage->mapGridY));
                readingAreas = true;
            } else if (readingAreas) {
                int mapIndex, gridX, gridY;
                if (sscanf(line, "area %d %d %d", &mapIndex, &gridX, &gridY) == 3 &&
                    mapIndex >= 0 && mapIndex < RPG_STAGE_MAP_COUNT) {
                    stage->mapActive[mapIndex] = true;
                    stage->mapGridX[mapIndex] = gridX;
                    stage->mapGridY[mapIndex] = gridY;
                }
            }
        }
        fclose(file);
        for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++)
            if (stage->mapActive[mapIndex]) FillMapGround(stage, mapIndex);
        return RpgStage_Save(filePath, stage);
    }
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
        int imageAppearance;
        int imageLayer;
        int imageHasCustomPosition;
        float imageScale;
        float imagePositionX;
        float imagePositionY;
        char path[RPG_STAGE_REFERENCE_PATH_LENGTH];
        if (sscanf(line, "reference %d %d %259[^\n]", &row, &column, path) == 3 &&
            row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
            RpgBlockInventory_IsReferenceObject(stage->blocks[row][column])) {
            NormalizeReferencePath(path, stage->referencePaths[row][column],
                                   RPG_STAGE_REFERENCE_PATH_LENGTH);
        } else if (sscanf(line, "image4 %d %d %d %d %f %d %f %f %259[^\n]", &row, &column, &imageAppearance,
                          &imageLayer, &imageScale, &imageHasCustomPosition, &imagePositionX, &imagePositionY,
                          path) == 9 && row >= 0 && row < RPG_STAGE_ROWS && column >= 0 &&
                   column < RPG_STAGE_WORLD_COLUMNS) {
            int imageIndex = RpgImageObjects_Add(&stage->imageObjects, row, column);
            if (imageIndex >= 0) {
                RpgImageObject *object = &stage->imageObjects.entries[imageIndex];
                object->scale = Clamp(imageScale, 0.25f, 8.0f);
                object->appearance = imageAppearance >= RPG_IMAGE_OBJECT_APPEARANCE_PNG &&
                                     imageAppearance <= RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FILE ?
                                     (RpgImageObjectAppearance)imageAppearance : RPG_IMAGE_OBJECT_APPEARANCE_PNG;
                object->layer = imageLayer >= RPG_IMAGE_OBJECT_LAYER_BACK && imageLayer < RPG_IMAGE_OBJECT_LAYER_COUNT ?
                                (RpgImageObjectLayer)imageLayer : RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK;
                object->hasCustomPosition = imageHasCustomPosition != 0;
                object->positionX = imagePositionX;
                object->positionY = imagePositionY;
                if (strcmp(path, "-") != 0) NormalizeReferencePath(path, object->path, sizeof(object->path));
            }
        } else if (sscanf(line, "image3 %d %d %d %d %f %259[^\n]", &row, &column, &imageAppearance,
                          &imageLayer, &imageScale, path) == 6 &&
                   row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
            int imageIndex = RpgImageObjects_Add(&stage->imageObjects, row, column);
            if (imageIndex >= 0) {
                RpgImageObject *object = &stage->imageObjects.entries[imageIndex];
                object->scale = Clamp(imageScale, 0.25f, 8.0f);
                object->appearance = imageAppearance >= RPG_IMAGE_OBJECT_APPEARANCE_PNG &&
                                     imageAppearance <= RPG_IMAGE_OBJECT_APPEARANCE_SHELL_FILE ?
                                     (RpgImageObjectAppearance)imageAppearance : RPG_IMAGE_OBJECT_APPEARANCE_PNG;
                object->layer = imageLayer >= RPG_IMAGE_OBJECT_LAYER_BACK && imageLayer < RPG_IMAGE_OBJECT_LAYER_COUNT ?
                                (RpgImageObjectLayer)imageLayer : RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK;
                if (strcmp(path, "-") != 0)
                    NormalizeReferencePath(path, object->path, sizeof(object->path));
            }
        } else if (sscanf(line, "image2 %d %d %d %f %259[^\n]", &row, &column, &imageLayer, &imageScale, path) == 5 &&
                   row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
            int imageIndex = RpgImageObjects_Add(&stage->imageObjects, row, column);
            if (imageIndex >= 0) {
                stage->imageObjects.entries[imageIndex].scale = Clamp(imageScale, 0.25f, 8.0f);
                stage->imageObjects.entries[imageIndex].layer =
                    imageLayer >= RPG_IMAGE_OBJECT_LAYER_BACK && imageLayer < RPG_IMAGE_OBJECT_LAYER_COUNT ?
                    (RpgImageObjectLayer)imageLayer : RPG_IMAGE_OBJECT_LAYER_BLOCK_FRONT_CHARACTER_BACK;
                if (strcmp(path, "-") != 0)
                    NormalizeReferencePath(path, stage->imageObjects.entries[imageIndex].path,
                                           sizeof(stage->imageObjects.entries[imageIndex].path));
            }
        } else if (sscanf(line, "image %d %d %f %259[^\n]", &row, &column, &imageScale, path) == 4 &&
                   row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS) {
            int imageIndex = RpgImageObjects_Add(&stage->imageObjects, row, column);
            if (imageIndex >= 0) {
                stage->imageObjects.entries[imageIndex].scale = Clamp(imageScale, 0.25f, 8.0f);
                if (strcmp(path, "-") != 0)
                    NormalizeReferencePath(path, stage->imageObjects.entries[imageIndex].path,
                                           sizeof(stage->imageObjects.entries[imageIndex].path));
            }
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
    fprintf(file, "grid %d %d %d\n", RPG_STAGE_TILE_SIZE, RPG_STAGE_COLUMNS, RPG_STAGE_ROWS);
    for (int row = 0; row < RPG_STAGE_ROWS; row++) {
        for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) fprintf(file, "%d ", stage->blocks[row][column]);
        fputc('\n', file);
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++)
        if (RpgBlockInventory_IsReferenceObject(stage->blocks[row][column]) && stage->referencePaths[row][column][0] != '\0')
            fprintf(file, "reference %d %d %s\n", row, column, stage->referencePaths[row][column]);
    for (int index = 0; index < stage->imageObjects.count; index++) {
        const RpgImageObject *object = &stage->imageObjects.entries[index];
        fprintf(file, "image4 %d %d %d %d %.3f %d %.3f %.3f %s\n", object->row, object->column,
                (int)object->appearance, (int)object->layer, object->scale, object->hasCustomPosition ? 1 : 0,
                object->positionX, object->positionY,
                object->path[0] != '\0' ? object->path : "-");
    }
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
    if (!RpgBlockInventory_IsReferenceObject(blockType)) {
        stage->referencePaths[row][column][0] = '\0';
    }
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
        !RpgBlockInventory_IsReferenceObject(stage->blocks[row][column]) || path == NULL) return false;
    snprintf(stage->referencePaths[row][column], RPG_STAGE_REFERENCE_PATH_LENGTH, "%s", path);
    return true;
}

const char *RpgStage_GetReferencePathAtCell(const RpgStage *stage, int row, int column)
{
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsReferenceObject(stage->blocks[row][column])) return "";
    return stage->referencePaths[row][column];
}

RpgReferenceObjects RpgReferenceObjects_Default(void) { return (RpgReferenceObjects){ 0 }; }

bool RpgReferenceObjects_AddDrop(RpgReferenceObjects *objects, Vector2 position, const char *path)
{
    if (objects == NULL || path == NULL || path[0] == '\0' || objects->count >= RPG_REFERENCE_OBJECT_MAX_COUNT) return false;
    RpgReferenceObject *object = &objects->entries[objects->count++];
    *object = (RpgReferenceObject){ .position = position, .isFalling = true, .drawScale = 1.0f };
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
        if (object->position.y >= RPG_STAGE_GROUND_TOP - 2.0f) {
            object->position.y = RPG_STAGE_GROUND_TOP - 2.0f;
            object->fallSpeed = 0.0f;
            object->isFalling = false;
        }
    }
}

/* 取得済みFileは通常の落下物とは別に、Zipperと同じく主人公の後ろへ補間移動する。 */
void RpgReferenceObjects_UpdateFollowers(RpgReferenceObjects *objects, Vector2 playerPosition,
                                         float playerScale, float playerMoveSpeed, float followerScale,
                                         float deltaTime)
{
    int followerIndex = 0;
    if (objects == NULL) return;
    for (int index = 0; index < objects->count; index++) {
        RpgReferenceObject *object = &objects->entries[index];
        Vector2 target;
        Vector2 distance;
        float length;
        float maximumStep;
        if (!object->followsPlayer) continue;
        target = (Vector2){ playerPosition.x - RPG_STAGE_TILE_SIZE * playerScale * (1.0f + followerIndex * 0.70f),
                            playerPosition.y };
        distance = Vector2Subtract(target, object->position);
        length = Vector2Length(distance);
        maximumStep = fmaxf(playerMoveSpeed, 120.0f) * deltaTime;
        if (length <= maximumStep) object->position = target;
        else if (length > 0.001f)
            object->position = Vector2Add(object->position, Vector2Scale(distance, maximumStep / length));
        object->drawScale = fmaxf(0.15f, fminf(followerScale, 1.0f));
        followerIndex++;
    }
}

/* 新しい追従対象へ切り替えるときだけ、先行Fileを通常の配置済みFileへ戻す。 */
static void RpgReferenceObjects_ReleaseFollowers(RpgReferenceObjects *objects)
{
    if (objects == NULL) return;
    for (int index = 0; index < objects->count; index++) {
        RpgReferenceObject *existingFollower = &objects->entries[index];
        if (!existingFollower->followsPlayer) continue;
        existingFollower->followsPlayer = false;
        existingFollower->drawScale = 1.0f;
        existingFollower->isFalling = false;
        existingFollower->fallSpeed = 0.0f;
    }
}

bool RpgReferenceObjects_CollectTarget(RpgStage *stage, RpgReferenceObjects *objects,
                                       RpgReferenceTarget target)
{
    RpgReferenceObject collected = { .drawScale = 1.0f, .followsPlayer = true };
    if (stage == NULL || objects == NULL) return false;
    if (target.kind == RPG_REFERENCE_TARGET_CELL) {
        if (target.row < 0 || target.row >= RPG_STAGE_ROWS || target.column < 0 ||
            target.column >= RPG_STAGE_WORLD_COLUMNS ||
            stage->blocks[target.row][target.column] != RPG_BLOCK_REFERENCE_FILE ||
            objects->count >= RPG_REFERENCE_OBJECT_MAX_COUNT) return false;
        /* 追従枠は一つだけにする。次のFileを取得する直前に、従来の落下物と同じ
           RpgReferenceObjectへ戻すため、位置・ファイル情報・マウス操作を保ったまま残せる。 */
        RpgReferenceObjects_ReleaseFollowers(objects);
        collected.position = (Vector2){ (target.column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                        (target.row + 0.5f) * RPG_STAGE_TILE_SIZE };
        snprintf(collected.path, sizeof(collected.path), "%s",
                 stage->referencePaths[target.row][target.column]);
        stage->blocks[target.row][target.column] = 0;
        stage->referencePaths[target.row][target.column][0] = '\0';
    } else if (target.kind == RPG_REFERENCE_TARGET_DROP && target.dropIndex >= 0 &&
               target.dropIndex < objects->count) {
        /* 既に配置済みのFileは同じオブジェクトを追従状態へ切り替える。 */
        RpgReferenceObjects_ReleaseFollowers(objects);
        RpgReferenceObject *object = &objects->entries[target.dropIndex];
        object->isFalling = false;
        object->fallSpeed = 0.0f;
        object->drawScale = 1.0f;
        object->followsPlayer = true;
        return true;
    } else return false;
    objects->entries[objects->count++] = collected;
    return true;
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
        float size = 48.0f * (object->drawScale > 0.0f ? object->drawScale : 1.0f);
        RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ object->position.x - size * 0.5f,
                                      object->position.y - size * 0.5f, size, size }, WHITE);
    }
}

int RpgReferenceObjects_FindNearby(const RpgReferenceObjects *objects, Vector2 position, float distance)
{
    int closestIndex = -1;
    float closestDistance = distance;
    for (int index = 0; index < objects->count; index++) {
        if (objects->entries[index].followsPlayer) continue;
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
        if (objects->entries[index].followsPlayer) continue;
        float objectDistance = Vector2Distance(position, objects->entries[index].position);
        if (objectDistance <= closestDistance) {
            closestDistance = objectDistance;
            *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_DROP, .row = -1,
                                             .column = -1, .dropIndex = index };
        }
    }
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!RpgBlockInventory_IsReferenceObject(stage->blocks[row][column])) continue;
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

bool RpgReferenceObjects_FindNearbyFolderTarget(const RpgStage *stage, Vector2 position,
                                                float distance, RpgReferenceTarget *target)
{
    float closestDistance = distance;
    if (stage == NULL || target == NULL) return false;
    *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_NONE, .row = -1,
                                    .column = -1, .dropIndex = -1 };
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0;
         column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (!RpgBlockInventory_IsReferenceFolder(stage->blocks[row][column])) continue;
        Vector2 folderPosition = { (column + 0.5f) * RPG_STAGE_TILE_SIZE,
                                   (row + 0.5f) * RPG_STAGE_TILE_SIZE };
        float folderDistance = Vector2Distance(position, folderPosition);
        if (folderDistance > closestDistance) continue;
        closestDistance = folderDistance;
        *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_CELL, .row = row,
                                        .column = column, .dropIndex = -1 };
    }
    return target->kind != RPG_REFERENCE_TARGET_NONE;
}

int RpgReferenceObjects_FindFollowerIndex(const RpgReferenceObjects *objects)
{
    if (objects == NULL) return -1;
    for (int index = 0; index < objects->count; index++)
        if (objects->entries[index].followsPlayer) return index;
    return -1;
}

bool RpgReferenceObjects_FindTarget(const RpgStage *stage, const RpgReferenceObjects *objects,
                                    Vector2 position, RpgReferenceTarget *target)
{
    if (target == NULL) return false;
    *target = (RpgReferenceTarget){ .kind = RPG_REFERENCE_TARGET_NONE, .row = -1, .column = -1, .dropIndex = -1 };
    for (int index = objects->count - 1; index >= 0; index--) {
        const RpgReferenceObject *object = &objects->entries[index];
        float size = 48.0f * (object->drawScale > 0.0f ? object->drawScale : 1.0f);
        /* 追従中も描画しているFile自身を対象にする。近接取得とは分離し、
           クリック・ドラッグ・ダブルクリックだけは常に同じFile操作へ流す。 */
        if (CheckCollisionPointRec(position, (Rectangle){ object->position.x - size * 0.5f,
            object->position.y - size * 0.5f, size, size })) {
            target->kind = RPG_REFERENCE_TARGET_DROP;
            target->dropIndex = index;
            return true;
        }
    }
    int column = (int)(position.x / RPG_STAGE_TILE_SIZE);
    int row = (int)(position.y / RPG_STAGE_TILE_SIZE);
    if (row < 0 || row >= RPG_STAGE_ROWS || column < 0 || column >= RPG_STAGE_WORLD_COLUMNS ||
        !RpgBlockInventory_IsReferenceObject(stage->blocks[row][column])) return false;
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
    Rectangle solidParts[2];
    int solidPartCount = RpgStage_GetHoleSolidParts(cell, blockType, solidParts);
    // 描画と同じ中央20pxの空洞以外だけを、壁の実体として判定する。
    for (int index = 0; index < solidPartCount; index++)
        if (CheckCollisionRecs(bounds, solidParts[index])) return true;
    if (solidPartCount > 0) return false;
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
        Rectangle solidParts[2];
        int solidPartCount = RpgStage_GetHoleSolidParts(cell, blockType, solidParts);
        if (solidPartCount > 0) {
            for (int partIndex = 0; partIndex < solidPartCount; partIndex++) {
                if (!CheckCollisionCircleRec(center, radius, solidParts[partIndex])) continue;
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
    if (blockType == RPG_BLOCK_BUILD_MISSING) return (Color){ 210, 45, 45, 255 };
    if (blockType == RPG_BLOCK_DOOR_OPEN_TOP) blockType = RPG_BLOCK_DOOR_CLOSED_TOP;
    return blockType >= 1 && blockType <= RPG_BLOCK_DOOR_OPEN_BOTTOM ? colors[blockType] :
           blockType == RPG_BLOCK_EFFECT_BUTTON ? (Color){ 72, 84, 104, 255 } : colors[1];
}

void RpgStage_DrawEffectSymbol(Rectangle cell, int blockType)
{
    const float center = RPG_STAGE_TILE_SIZE * 0.5f;
    if (blockType == RPG_BLOCK_EFFECT_BOUNCE)
        DrawCircle((int)(cell.x + center), (int)(cell.y + center), 8.0f, RAYWHITE);
    if (blockType == RPG_BLOCK_EFFECT_SLOW)
        DrawTriangle((Vector2){ cell.x + 8.0f, cell.y + 24.0f },
                     (Vector2){ cell.x + 24.0f, cell.y + 16.0f },
                     (Vector2){ cell.x + 8.0f, cell.y + 8.0f }, RAYWHITE);
    if (blockType == RPG_BLOCK_EFFECT_WIDE_BOUNCE) {
        DrawRectangleRec((Rectangle){ cell.x + 4.0f, cell.y + 12.0f,
                                      RPG_STAGE_TILE_SIZE * 2.0f - 8.0f, 9.0f }, RAYWHITE);
        DrawCircle((int)(cell.x + 12.0f), (int)(cell.y + center), 5.0f, ORANGE);
        DrawCircle((int)(cell.x + RPG_STAGE_TILE_SIZE * 2.0f - 12.0f),
                   (int)(cell.y + center), 5.0f, ORANGE);
    }
    if (blockType == RPG_BLOCK_EFFECT_CORNER_BOUNCE) {
        DrawLineEx((Vector2){ cell.x + 7.0f, cell.y + 8.0f },
                   (Vector2){ cell.x + RPG_STAGE_TILE_SIZE * 2.0f - 7.0f, cell.y + 8.0f }, 3.0f, RAYWHITE);
        DrawLineEx((Vector2){ cell.x + 7.0f, cell.y + 8.0f },
                   (Vector2){ cell.x + 7.0f, cell.y + RPG_STAGE_TILE_SIZE * 2.0f - 7.0f }, 3.0f, RAYWHITE);
    }
    if (blockType == RPG_BLOCK_DOOR_CLOSED_TOP) {
        DrawRectangleLinesEx((Rectangle){ cell.x + 10.0f, cell.y + 4.0f, 12.0f,
                                           RPG_STAGE_TILE_SIZE * 3.0f - 8.0f }, 2.0f, GOLD);
    }
    if (blockType == RPG_BLOCK_DOOR_OPEN_TOP) {
        DrawCircleV((Vector2){ cell.x + center, cell.y + 9.0f }, 9.0f, Fade(YELLOW, 0.35f));
        DrawCircleV((Vector2){ cell.x + center, cell.y + 9.0f }, 4.0f, YELLOW);
        DrawLineEx((Vector2){ cell.x + 5.0f, cell.y + 9.0f },
                   (Vector2){ cell.x + RPG_STAGE_TILE_SIZE - 5.0f, cell.y + 9.0f }, 2.0f, RAYWHITE);
    }
    if (blockType == RPG_BLOCK_EFFECT_BUTTON) {
        DrawRectangleRec((Rectangle){ cell.x + 5.0f, cell.y + 20.0f, 22.0f, 8.0f }, DARKGRAY);
        DrawRectangleLinesEx((Rectangle){ cell.x + 5.0f, cell.y + 20.0f, 22.0f, 8.0f }, 2.0f, RAYWHITE);
        DrawCircle((int)(cell.x + center), (int)(cell.y + 19.0f), 5.0f, RED);
    }
    // 伸縮後にも残る根元マスを、紫のアンカー記号で明確に示す。
    if (blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_HORIZONTAL ||
        blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL ||
        blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT ||
        blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP) {
        Vector2 root = { cell.x + center, cell.y + center };
        DrawCircle((int)root.x, (int)root.y, 8.0f, Fade(PURPLE, 0.55f));
        DrawCircleLines((int)root.x, (int)root.y, 8.0f, RAYWHITE);
        Vector2 end = blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_VERTICAL ?
            (Vector2){ root.x, cell.y + RPG_STAGE_TILE_SIZE - 5.0f } :
            blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_LEFT ? (Vector2){ cell.x + 5.0f, root.y } :
            blockType == RPG_BLOCK_SIGNAL_SHRINK_ROOT_UP ? (Vector2){ root.x, cell.y + 5.0f } :
            (Vector2){ cell.x + RPG_STAGE_TILE_SIZE - 5.0f, root.y };
        DrawLineEx(root, end, 3.0f, GOLD);
    }
}

/* ブロックは常に不透明に保ち、RGB成分だけを明るさ倍率で補正する。 */
static Color ApplyBlockBrightness(Color color, float brightness)
{
    color.r = (unsigned char)((float)color.r * brightness + 0.5f);
    color.g = (unsigned char)((float)color.g * brightness + 0.5f);
    color.b = (unsigned char)((float)color.b * brightness + 0.5f);
    return color;
}

int RpgStage_GetHoleSolidParts(Rectangle cell, int blockType, Rectangle solidParts[2])
{
    // 穴は縦・横とも中央20px。余白側の6pxずつだけを描画・衝突判定に使う。
    const float holeSize = 20.0f;
    if (solidParts == NULL) return 0;
    if (blockType == RPG_BLOCK_HOLE_VERTICAL) {
        float sideWidth = (cell.width - holeSize) * 0.5f;
        solidParts[0] = (Rectangle){ cell.x, cell.y, sideWidth, cell.height };
        solidParts[1] = (Rectangle){ cell.x + sideWidth + holeSize, cell.y, sideWidth, cell.height };
        return 2;
    }
    if (blockType == RPG_BLOCK_HOLE_HORIZONTAL) {
        float sideHeight = (cell.height - holeSize) * 0.5f;
        solidParts[0] = (Rectangle){ cell.x, cell.y, cell.width, sideHeight };
        solidParts[1] = (Rectangle){ cell.x, cell.y + sideHeight + holeSize, cell.width, sideHeight };
        return 2;
    }
    return 0;
}

void RpgStage_DrawBlockCell(Rectangle cell, int blockType, float brightness)
{
    Color color = ApplyBlockBrightness(RpgStage_GetBlockColor(blockType), brightness);
    Rectangle solidParts[2];
    int solidPartCount = RpgStage_GetHoleSolidParts(cell, blockType, solidParts);
    // 穴ブロックは、衝突判定と同じ実体部分だけを描画する。
    if (solidPartCount > 0) {
        for (int index = 0; index < solidPartCount; index++) DrawRectangleRec(solidParts[index], color);
    } else if (RpgStage_IsSolidBlock(blockType)) {
        DrawRectangleRec(cell, color);
    }
}

void RpgStage_Draw(const RpgStage *stage, bool showGrid, float brightness)
{
    if (brightness < 0.15f) brightness = 0.15f;
    if (brightness > 1.0f) brightness = 1.0f;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        int blockType = stage->blocks[row][column];
        // 開いたドアの下2マスは空白、穴付きブロックは穴の外側だけを描画する。
        RpgStage_DrawBlockCell(cell, blockType, brightness);
        if (showGrid) DrawRectangleLinesEx(cell, 1.0f, Fade(DARKGRAY, 0.45f));
    }
}

void RpgStage_DrawMap(const RpgStage *stage, int mapIndex, bool showGrid, float brightness)
{
    if (brightness < 0.15f) brightness = 0.15f;
    if (brightness > 1.0f) brightness = 1.0f;
    int startColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        int blockType = stage->blocks[row][startColumn + column];
        // マップ表示もゲーム本編と同じ壁形状を描画する。
        RpgStage_DrawBlockCell(cell, blockType, brightness);
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

void RpgStage_DrawReferenceFolder(Rectangle cell, Color tint)
{
    /* Windowsの標準フォルダを参考にした軽量なベクター表示。画像資産を増やさない。 */
    Color folder = ColorAlpha((Color){ 244, 183, 55, 255 }, tint.a / 255.0f);
    DrawRectangleRec((Rectangle){ cell.x + 4.0f, cell.y + 11.0f, cell.width - 8.0f, cell.height - 16.0f }, folder);
    DrawRectangleRec((Rectangle){ cell.x + 6.0f, cell.y + 7.0f, cell.width * 0.43f, 7.0f }, folder);
    DrawRectangleLinesEx((Rectangle){ cell.x + 4.0f, cell.y + 11.0f, cell.width - 8.0f, cell.height - 16.0f }, 1.0f, Fade(BROWN, 0.70f));
}

/* 同じPNGを毎フレーム読み込まないため、Folder配置物だけの小さなTextureキャッシュを保持する。 */
void RpgStage_DrawReferenceObjects(const RpgStage *stage, Texture2D fileTexture)
{
    RpgStage_DrawReferenceObjectsExcept(stage, fileTexture, -1, -1);
}

void RpgStage_DrawReferenceObjectsExcept(const RpgStage *stage, Texture2D fileTexture,
                                         int excludedRow, int excludedColumn)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        if (row == excludedRow && column == excludedColumn) continue;
        if (stage->blocks[row][column] == RPG_BLOCK_REFERENCE_FILE)
            RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                                                     RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }, WHITE);
        else if (stage->blocks[row][column] == RPG_BLOCK_REFERENCE_FOLDER)
            RpgStage_DrawReferenceFolder((Rectangle){ column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                                       RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }, WHITE);
    }
}

void RpgStage_DrawMapReferenceObjects(const RpgStage *stage, int mapIndex, Texture2D fileTexture)
{
    int startColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_COLUMNS; column++)
        if (stage->blocks[row][startColumn + column] == RPG_BLOCK_REFERENCE_FILE)
            RpgStage_DrawReferenceObject(fileTexture, (Rectangle){ column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                                                     RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }, WHITE);
        else if (stage->blocks[row][startColumn + column] == RPG_BLOCK_REFERENCE_FOLDER)
            RpgStage_DrawReferenceFolder((Rectangle){ column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                                                       RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE }, WHITE);
}

// 地面や前景を描いた後にも特殊効果の記号だけを前面へ出せるようにする。
void RpgStage_DrawEffects(const RpgStage *stage)
{
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_WORLD_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        RpgStage_DrawEffectSymbol(cell, stage->blocks[row][column]);
    }
}

void RpgStage_DrawMapEffects(const RpgStage *stage, int mapIndex)
{
    int startColumn = mapIndex * RPG_STAGE_COLUMNS;
    for (int row = 0; row < RPG_STAGE_ROWS; row++) for (int column = 0; column < RPG_STAGE_COLUMNS; column++) {
        Rectangle cell = { column * RPG_STAGE_TILE_SIZE, row * RPG_STAGE_TILE_SIZE,
                           RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
        RpgStage_DrawEffectSymbol(cell, stage->blocks[row][startColumn + column]);
    }
}
// 役割: RPG ステージのグリッド、地形判定、特殊ブロック、描画を管理する。
