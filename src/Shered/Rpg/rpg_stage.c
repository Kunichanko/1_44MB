// 依存する自プロジェクト内ファイル: rpg_block_inventory.h, rpg_stage.h, rpg_viewport.h
// 依存関係を更新: 特殊ブロックの種類と現在の仮想表示倍率を参照する。
#include "rpg_stage.h"

#include "raymath.h"

#include "rpg_block_inventory.h"
#include "rpg_gimic_sprites.h"
#include "rpg_viewport.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RPG_REFERENCE_COMPRESSION_DURATION 0.48f
static Texture2D referenceCompressAnimationTexture = { 0 };
static Texture2D referenceCompressedTexture = { 0 };
static Texture2D groundTexture = { 0 };
static float groundHue = 0.50f;
static float groundSaturation = 0.50f;
static float groundLightness = 0.50f;
static Shader groundAdjustmentShader = { 0 };
static int groundHueLocation = -1;
static int groundSaturationLocation = -1;
static int groundLightnessLocation = -1;

static const char *groundAdjustmentFragmentShader =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform float groundHue;\n"
    "uniform float groundSaturation;\n"
    "uniform float groundLightness;\n"
    "out vec4 finalColor;\n"
    "vec3 rgbToHsl(vec3 c) {\n"
    "  float hi=max(max(c.r,c.g),c.b), lo=min(min(c.r,c.g),c.b), d=hi-lo, l=(hi+lo)*0.5;\n"
    "  if(d<0.00001) return vec3(0.0,0.0,l);\n"
    "  float s=d/(1.0-abs(2.0*l-1.0)), h;\n"
    "  if(hi==c.r) h=mod((c.g-c.b)/d,6.0); else if(hi==c.g) h=(c.b-c.r)/d+2.0; else h=(c.r-c.g)/d+4.0;\n"
    "  return vec3(h/6.0,s,l);\n"
    "}\n"
    "float hueChannel(float p,float q,float t) { if(t<0.0)t+=1.0; if(t>1.0)t-=1.0; if(t<1.0/6.0)return p+(q-p)*6.0*t; if(t<0.5)return q; if(t<2.0/3.0)return p+(q-p)*(2.0/3.0-t)*6.0; return p; }\n"
    "vec3 hslToRgb(vec3 hsl) { if(hsl.y<0.00001)return vec3(hsl.z); float q=hsl.z<0.5?hsl.z*(1.0+hsl.y):hsl.z+hsl.y-hsl.z*hsl.y, p=2.0*hsl.z-q; return vec3(hueChannel(p,q,hsl.x+1.0/3.0),hueChannel(p,q,hsl.x),hueChannel(p,q,hsl.x-1.0/3.0)); }\n"
    "void main() { vec4 c=texture(texture0,fragTexCoord)*colDiffuse*fragColor; vec3 hsl=rgbToHsl(c.rgb); hsl.x=fract(hsl.x+(groundHue-0.5)); hsl.y=clamp(hsl.y*(groundSaturation*2.0),0.0,1.0); hsl.z=clamp(hsl.z*(groundLightness*2.0),0.0,1.0); finalColor=vec4(hslToRgb(hsl),c.a); }\n";

static void EnsureReferenceCompressionTextures(void)
{
    if (referenceCompressAnimationTexture.id != 0 && referenceCompressedTexture.id != 0) return;
    const char *directory = GetApplicationDirectory();
    char animationPath[1024], compressedPath[1024];
    snprintf(animationPath, sizeof(animationPath), "%s../assets/Sprite/CompressAnim.png", directory);
    snprintf(compressedPath, sizeof(compressedPath), "%s../assets/Sprite/Compress.png", directory);
    if (referenceCompressAnimationTexture.id == 0) referenceCompressAnimationTexture = LoadTexture(animationPath);
    if (referenceCompressedTexture.id == 0) referenceCompressedTexture = LoadTexture(compressedPath);
    if (referenceCompressAnimationTexture.id != 0) SetTextureFilter(referenceCompressAnimationTexture, TEXTURE_FILTER_POINT);
    if (referenceCompressedTexture.id != 0) SetTextureFilter(referenceCompressedTexture, TEXTURE_FILTER_POINT);
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

float RpgStage_SnapRenderCoordinate(float coordinate)
{
    // 本編は等倍、エディターは従来の仮想表示倍率へそろえて、どちらも半端な描画座標を残さない。
    const float displayScale = (float)RpgViewport_GetWidth() /
                               (float)(RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE);
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
    else if (direction == RPG_AREA_UP) *y = 1;
    else if (direction == RPG_AREA_DOWN) *y = -1;
}

/* Grid coordinates are user-facing area IDs.  Keep the lowest occupied x and
   y at zero, so every stage always has an Area[x][0] and Area[0][y] rather
   than negative area names.  Storage slots are deliberately untouched; only
   the continuous-world projection changes by a uniform offset. */
static void NormalizeMapGridOrigin(RpgStage *stage)
{
    int minX = 0;
    int minY = 0;
    bool foundActiveMap = false;
    if (stage == NULL) return;

    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        if (!stage->mapActive[index]) continue;
        if (!foundActiveMap) {
            minX = stage->mapGridX[index];
            minY = stage->mapGridY[index];
            foundActiveMap = true;
        } else {
            if (stage->mapGridX[index] < minX) minX = stage->mapGridX[index];
            if (stage->mapGridY[index] < minY) minY = stage->mapGridY[index];
        }
    }
    if (!foundActiveMap || (minX == 0 && minY == 0)) return;

    const int shiftX = -minX;
    const int shiftY = -minY;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        if (!stage->mapActive[index]) continue;
        stage->mapGridX[index] += shiftX;
        stage->mapGridY[index] += shiftY;
    }
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

int RpgStage_GetMapAtWorldPosition(const RpgStage *stage, Vector2 position)
{
    const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    const float mapHeight = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE;
    int gridX;
    int gridY;
    if (stage == NULL) return -1;
    gridX = (int)floorf(position.x / mapWidth);
    gridY = -(int)floorf(position.y / mapHeight);
    return RpgStage_GetMapAtGrid(stage, gridX, gridY);
}

bool RpgStage_GetWorldCellAtPosition(const RpgStage *stage, Vector2 position,
                                     int *storageRow, int *storageColumn)
{
    const float mapWidth = RPG_STAGE_COLUMNS * RPG_STAGE_TILE_SIZE;
    const float mapHeight = RPG_STAGE_ROWS * RPG_STAGE_TILE_SIZE;
    int mapIndex;
    int gridX;
    int gridY;
    int localColumn;
    int localRow;
    if (stage == NULL) return false;
    gridX = (int)floorf(position.x / mapWidth);
    gridY = -(int)floorf(position.y / mapHeight);
    mapIndex = RpgStage_GetMapAtGrid(stage, gridX, gridY);
    if (mapIndex < 0) return false;
    localColumn = (int)floorf((position.x - gridX * mapWidth) / RPG_STAGE_TILE_SIZE);
    localRow = (int)floorf((position.y + gridY * mapHeight) / RPG_STAGE_TILE_SIZE);
    if (localColumn < 0 || localColumn >= RPG_STAGE_COLUMNS ||
        localRow < 0 || localRow >= RPG_STAGE_ROWS) return false;
    if (storageColumn != NULL) *storageColumn = mapIndex * RPG_STAGE_COLUMNS + localColumn;
    if (storageRow != NULL) *storageRow = localRow;
    return true;
}

Vector2 RpgStage_GetWorldPositionForCell(const RpgStage *stage, int row, int column)
{
    int mapIndex;
    int localColumn;
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS ||
        column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return (Vector2){ 0.0f, 0.0f };
    mapIndex = column / RPG_STAGE_COLUMNS;
    localColumn = column % RPG_STAGE_COLUMNS;
    if (!RpgStage_IsMapActive(stage, mapIndex)) return (Vector2){ 0.0f, 0.0f };
    return (Vector2){ (stage->mapGridX[mapIndex] * RPG_STAGE_COLUMNS + localColumn + 0.5f) * RPG_STAGE_TILE_SIZE,
                      ((-stage->mapGridY[mapIndex] * RPG_STAGE_ROWS) + row + 0.5f) * RPG_STAGE_TILE_SIZE };
}

Rectangle RpgStage_GetWorldBoundsForCell(const RpgStage *stage, int row, int column)
{
    Vector2 center = RpgStage_GetWorldPositionForCell(stage, row, column);
    return (Rectangle){ center.x - RPG_STAGE_TILE_SIZE * 0.5f, center.y - RPG_STAGE_TILE_SIZE * 0.5f,
                        RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
}

static int CountConnectedMapComponents(const RpgStage *stage)
{
    bool visited[RPG_STAGE_MAP_COUNT] = { false };
    int queue[RPG_STAGE_MAP_COUNT];
    int componentCount = 0;
    if (stage == NULL) return 0;
    for (int start = 0; start < RPG_STAGE_MAP_COUNT; start++) {
        if (!stage->mapActive[start] || visited[start]) continue;
        int head = 0;
        int tail = 0;
        queue[tail++] = start;
        visited[start] = true;
        componentCount++;
        while (head < tail) {
            int map = queue[head++];
            const int x = stage->mapGridX[map];
            const int y = stage->mapGridY[map];
            const int neighborX[4] = { x - 1, x + 1, x, x };
            const int neighborY[4] = { y, y, y - 1, y + 1 };
            for (int direction = 0; direction < 4; direction++) {
                int neighbor = RpgStage_GetMapAtGrid(stage, neighborX[direction], neighborY[direction]);
                if (neighbor >= 0 && !visited[neighbor]) {
                    visited[neighbor] = true;
                    queue[tail++] = neighbor;
                }
            }
        }
    }
    return componentCount;
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

void RpgStage_SetSpatialReferenceMap(RpgStage *stage, int mapIndex)
{
    if (stage == NULL) return;
    stage->spatialReferenceMap = RpgStage_IsMapActive(stage, mapIndex) ? mapIndex : -1;
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
    NormalizeMapGridOrigin(stage);
    return freeIndex;
}

int RpgStage_InsertAdjacentMap(RpgStage *stage, int mapIndex, RpgAreaDirection direction)
{
    mapIndex = RpgStage_FindNearestActiveMap(stage, mapIndex);
    if (mapIndex < 0) return -1;
    int freeIndex = -1;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        if (!stage->mapActive[index]) {
            freeIndex = index;
            break;
        }
    }
    if (freeIndex < 0) return -1;

    int offsetX, offsetY;
    GetDirectionOffset(direction, &offsetX, &offsetY);
    int insertionX = stage->mapGridX[mapIndex] + offsetX;
    int insertionY = stage->mapGridY[mapIndex] + offsetY;
    /* A gap does not need insertion-style compaction.  Only move the
       consecutive occupied run beginning at the requested target, keeping
       unrelated gaps and later areas in place. */
    if (RpgStage_GetMapAtGrid(stage, insertionX, insertionY) >= 0) {
        int runStart = insertionX;
        int runEnd = insertionX;
        if (offsetX > 0) {
            while (RpgStage_GetMapAtGrid(stage, runEnd, insertionY) >= 0) runEnd++;
            for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
                if (stage->mapActive[index] && stage->mapGridY[index] == insertionY &&
                    stage->mapGridX[index] >= insertionX && stage->mapGridX[index] < runEnd)
                    stage->mapGridX[index]++;
        } else if (offsetX < 0) {
            while (RpgStage_GetMapAtGrid(stage, runStart, insertionY) >= 0) runStart--;
            for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
                if (stage->mapActive[index] && stage->mapGridY[index] == insertionY &&
                    stage->mapGridX[index] > runStart && stage->mapGridX[index] <= insertionX)
                    stage->mapGridX[index]--;
        } else if (offsetY > 0) {
            runEnd = insertionY;
            while (RpgStage_GetMapAtGrid(stage, insertionX, runEnd) >= 0) runEnd++;
            for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
                if (stage->mapActive[index] && stage->mapGridX[index] == insertionX &&
                    stage->mapGridY[index] >= insertionY && stage->mapGridY[index] < runEnd)
                    stage->mapGridY[index]++;
        } else if (offsetY < 0) {
            runStart = insertionY;
            while (RpgStage_GetMapAtGrid(stage, insertionX, runStart) >= 0) runStart--;
            for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++)
                if (stage->mapActive[index] && stage->mapGridX[index] == insertionX &&
                    stage->mapGridY[index] > runStart && stage->mapGridY[index] <= insertionY)
                    stage->mapGridY[index]--;
        }
    }
    ClearMapSlot(stage, freeIndex);
    FillMapGround(stage, freeIndex);
    stage->mapActive[freeIndex] = true;
    stage->mapGridX[freeIndex] = insertionX;
    stage->mapGridY[freeIndex] = insertionY;
    NormalizeMapGridOrigin(stage);
    return freeIndex;
}

bool RpgStage_RemoveMap(RpgStage *stage, int mapIndex)
{
    if (!RpgStage_IsMapActive(stage, mapIndex) || RpgStage_GetMapCount(stage) <= 1) return false;
    const int removedX = stage->mapGridX[mapIndex];
    const int removedY = stage->mapGridY[mapIndex];
    const int componentsBeforeRemoval = CountConnectedMapComponents(stage);
    bool hasHigherX = false, hasHigherY = false;
    for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
        if (index == mapIndex || !stage->mapActive[index]) continue;
        if (stage->mapGridY[index] == removedY) {
            if (stage->mapGridX[index] > removedX) hasHigherX = true;
        }
        if (stage->mapGridX[index] == removedX) {
            if (stage->mapGridY[index] > removedY) hasHigherY = true;
        }
    }
    ClearMapSlot(stage, mapIndex);
    stage->mapActive[mapIndex] = false;
    /* Preserve deliberate gaps.  A shift is only useful when this deletion
       actually split a formerly connected stage into isolated components. */
    if (CountConnectedMapComponents(stage) > componentsBeforeRemoval) {
        for (int index = 0; index < RPG_STAGE_MAP_COUNT; index++) {
            if (!stage->mapActive[index]) continue;
            /* Compact toward the lower coordinate only.  Do not pull
               lower-numbered areas upward/rightward. */
            if (hasHigherX && stage->mapGridY[index] == removedY &&
                stage->mapGridX[index] > removedX) stage->mapGridX[index]--;
            else if (!hasHigherX && hasHigherY && stage->mapGridX[index] == removedX &&
                     stage->mapGridY[index] > removedY) stage->mapGridY[index]--;
        }
    }
    NormalizeMapGridOrigin(stage);
    // 再作成前の二次元IDを残し、Revertなどで無効なスロットを参照した際の最寄り判定に使う。
    return true;
}

bool RpgStage_MoveMapToGrid(RpgStage *stage, int mapIndex, int gridX, int gridY)
{
    if (!RpgStage_IsMapActive(stage, mapIndex)) return false;
    int occupant = RpgStage_GetMapAtGrid(stage, gridX, gridY);
    if (occupant >= 0 && occupant != mapIndex) return false;
    stage->mapGridX[mapIndex] = gridX;
    stage->mapGridY[mapIndex] = gridY;
    NormalizeMapGridOrigin(stage);
    return true;
}

void RpgStage_Initialize(RpgStage *stage)
{
    if (stage == NULL) return;
    memset(stage, 0, sizeof(*stage));
    stage->spatialReferenceMap = -1;
    stage->imageObjects = RpgImageObjects_Default();
    for (int mapIndex = 0; mapIndex < RPG_STAGE_INITIAL_MAP_COUNT; mapIndex++) {
        stage->mapActive[mapIndex] = true;
        stage->mapGridX[mapIndex] = mapIndex;
        stage->mapGridY[mapIndex] = 0;
        FillMapGround(stage, mapIndex);
    }
}

bool RpgStage_Load(const char *filePath, RpgStage *stage)
{
    FILE *file = fopen(filePath, "r");
    char line[2048];
    bool loadedAny = false;
    if (file == NULL) return false;
    RpgStage_Initialize(stage);
    memset(stage->referencePaths, 0, sizeof(stage->referencePaths));
    int savedTileSize = 0, savedColumns = 0, savedRows = 0;
    bool isCurrentGrid = fgets(line, sizeof(line), file) != NULL &&
                         sscanf(line, "grid %d %d %d", &savedTileSize, &savedColumns, &savedRows) == 3 &&
                         savedTileSize == RPG_STAGE_TILE_SIZE && savedColumns == RPG_STAGE_COLUMNS &&
                         savedRows == RPG_STAGE_ROWS;
    // ステージ保存は常に現在の20x12形式だけを読む。変換は編集データとパッケージ生成時に完了させる。
    if (!isCurrentGrid) { fclose(file); return false; }
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
        char failureText[RPG_KEY_DOOR_FAILURE_TEXT_LENGTH];
        if (sscanf(line, "reference %d %d %259[^\n]", &row, &column, path) == 3 &&
            row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
            RpgBlockInventory_IsReferenceObject(stage->blocks[row][column])) {
            NormalizeReferencePath(path, stage->referencePaths[row][column],
                                   RPG_STAGE_REFERENCE_PATH_LENGTH);
        } else if (sscanf(line, "keydoor %d %d %259[^|]|%191[^\n]", &row, &column, path, failureText) == 4 &&
                   row >= 0 && row < RPG_STAGE_ROWS && column >= 0 && column < RPG_STAGE_WORLD_COLUMNS &&
                   RpgBlockInventory_IsKeyDoorBlock(stage->blocks[row][column])) {
            RpgKeyDoor *door = RpgStage_EnsureKeyDoor(stage, row, column);
            if (door != NULL) {
                NormalizeReferencePath(path, door->keyPath, sizeof(door->keyPath));
                snprintf(door->failureText, sizeof(door->failureText), "%s", failureText);
            }
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
            if (sscanf(line, "area2d %d %d %d", &gridX, &gridY, &mapIndex) == 3 &&
                mapIndex >= 0 && mapIndex < RPG_STAGE_MAP_COUNT) {
                stage->mapActive[mapIndex] = true;
                stage->mapGridX[mapIndex] = gridX;
                stage->mapGridY[mapIndex] = gridY;
            } else if (sscanf(line, "area %d %d %d", &mapIndex, &gridX, &gridY) == 3 &&
                mapIndex >= 0 && mapIndex < RPG_STAGE_MAP_COUNT) {
                stage->mapActive[mapIndex] = true;
                stage->mapGridX[mapIndex] = gridX;
                stage->mapGridY[mapIndex] = gridY;
            }
        }
    }
    NormalizeMapGridOrigin(stage);
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
    for (int index = 0; index < stage->keyDoorCount; index++) {
        const RpgKeyDoor *door = &stage->keyDoors[index];
        if (door->rootRow < 0 || door->rootRow >= RPG_STAGE_ROWS || door->rootColumn < 0 ||
            door->rootColumn >= RPG_STAGE_WORLD_COLUMNS ||
            !RpgBlockInventory_IsKeyDoorBlock(stage->blocks[door->rootRow][door->rootColumn])) continue;
        fprintf(file, "keydoor %d %d %s|%s\n", door->rootRow, door->rootColumn,
                door->keyPath, door->failureText);
    }
    for (int index = 0; index < stage->imageObjects.count; index++) {
        const RpgImageObject *object = &stage->imageObjects.entries[index];
        fprintf(file, "image4 %d %d %d %d %.3f %d %.3f %.3f %s\n", object->row, object->column,
                (int)object->appearance, (int)object->layer, object->scale, object->hasCustomPosition ? 1 : 0,
                object->positionX, object->positionY,
                object->path[0] != '\0' ? object->path : "-");
    }
    fputs("areas_begin\n", file);
    for (int mapIndex = 0; mapIndex < RPG_STAGE_MAP_COUNT; mapIndex++)
        if (stage->mapActive[mapIndex]) fprintf(file, "area2d %d %d %d\n",
                                                stage->mapGridX[mapIndex], stage->mapGridY[mapIndex], mapIndex);
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
    int column;
    int row;
    if (!RpgStage_GetWorldCellAtPosition(stage, position, &row, &column)) return 0;
    return stage->blocks[row][column];
}

bool RpgStage_SetDoorOpenAtCell(RpgStage *stage, int row, int column, bool isOpen)
{
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
        column >= RPG_STAGE_WORLD_COLUMNS || !RpgBlockInventory_IsDoorBlock(stage->blocks[row][column]))
        return false;
    const RpgEffectShape *currentShape = RpgBlockInventory_GetEffectShape(stage->blocks[row][column]);
    const bool isKeyDoor = RpgBlockInventory_IsKeyDoorBlock(stage->blocks[row][column]);
    const RpgEffectShape *targetShape = isKeyDoor ? RpgBlockInventory_GetKeyDoorShape(isOpen) :
                                                    RpgBlockInventory_GetDoorShape(isOpen);
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

RpgKeyDoor *RpgStage_GetKeyDoorAtCell(RpgStage *stage, int row, int column)
{
    int rootRow, rootColumn;
    if (stage == NULL || row < 0 || row >= RPG_STAGE_ROWS || column < 0 ||
        column >= RPG_STAGE_WORLD_COLUMNS || !RpgBlockInventory_IsKeyDoorBlock(stage->blocks[row][column]) ||
        !RpgStage_FindEffectRootCell(stage, row, column, &rootRow, &rootColumn, NULL)) return NULL;
    for (int index = 0; index < stage->keyDoorCount; index++)
        if (stage->keyDoors[index].rootRow == rootRow && stage->keyDoors[index].rootColumn == rootColumn)
            return &stage->keyDoors[index];
    return NULL;
}

const RpgKeyDoor *RpgStage_GetKeyDoorAtCellConst(const RpgStage *stage, int row, int column)
{
    return RpgStage_GetKeyDoorAtCell((RpgStage *)stage, row, column);
}

RpgKeyDoor *RpgStage_EnsureKeyDoor(RpgStage *stage, int row, int column)
{
    RpgKeyDoor *door = RpgStage_GetKeyDoorAtCell(stage, row, column);
    if (door != NULL) return door;
    if (stage == NULL || stage->keyDoorCount >= RPG_KEY_DOOR_MAX_COUNT) return NULL;
    int rootRow, rootColumn;
    if (!RpgStage_FindEffectRootCell(stage, row, column, &rootRow, &rootColumn, NULL)) return NULL;
    door = &stage->keyDoors[stage->keyDoorCount++];
    *door = (RpgKeyDoor){ .rootRow = rootRow, .rootColumn = rootColumn };
    snprintf(door->failureText, sizeof(door->failureText), "This door needs its key file.");
    return door;
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
        if (object->isCompressing) {
            object->compressionElapsed += deltaTime;
            if (object->compressionElapsed >= RPG_REFERENCE_COMPRESSION_DURATION) {
                object->compressionElapsed = RPG_REFERENCE_COMPRESSION_DURATION;
                object->isCompressing = false;
                object->isCompressed = true;
                object->followsPlayer = true;
            }
            continue;
        }
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
        existingFollower->isCompressing = false;
        existingFollower->isCompressed = false;
        existingFollower->drawScale = 1.0f;
        existingFollower->isFalling = false;
        existingFollower->fallSpeed = 0.0f;
    }
}

bool RpgReferenceObjects_CollectTarget(RpgStage *stage, RpgReferenceObjects *objects,
                                       RpgReferenceTarget target)
{
    RpgReferenceObject collected = { .drawScale = 1.0f, .isCompressing = true };
    if (stage == NULL || objects == NULL) return false;
    if (target.kind == RPG_REFERENCE_TARGET_CELL) {
        if (target.row < 0 || target.row >= RPG_STAGE_ROWS || target.column < 0 ||
            target.column >= RPG_STAGE_WORLD_COLUMNS ||
            stage->blocks[target.row][target.column] != RPG_BLOCK_REFERENCE_FILE ||
            objects->count >= RPG_REFERENCE_OBJECT_MAX_COUNT) return false;
        /* 追従枠は一つだけにする。次のFileを取得する直前に、従来の落下物と同じ
           RpgReferenceObjectへ戻すため、位置・ファイル情報・マウス操作を保ったまま残せる。 */
        RpgReferenceObjects_ReleaseFollowers(objects);
        collected.position = RpgStage_GetWorldPositionForCell(stage, target.row, target.column);
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
        object->followsPlayer = false;
        object->isCompressing = true;
        object->isCompressed = false;
        object->compressionElapsed = 0.0f;
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
    EnsureReferenceCompressionTextures();
    for (int index = 0; index < objects->count; index++) {
        if (index == excludedIndex) continue;
        const RpgReferenceObject *object = &objects->entries[index];
        float size = 48.0f * (object->drawScale > 0.0f ? object->drawScale : 1.0f);
        Rectangle destination = { object->position.x - size * 0.5f, object->position.y - size * 0.5f, size, size };
        if (object->isCompressing && referenceCompressAnimationTexture.id != 0) {
            int frame = Clamp((int)(object->compressionElapsed / RPG_REFERENCE_COMPRESSION_DURATION * 16.0f), 0, 15);
            Rectangle source = { (float)((frame % 4) * 32), (float)((frame / 4) * 40), 32.0f, 40.0f };
            destination.y -= size * 0.125f;
            destination.height = size * 1.25f;
            DrawTexturePro(referenceCompressAnimationTexture, source, destination, (Vector2){ 0 }, 0.0f, WHITE);
        } else if (object->isCompressed && referenceCompressedTexture.id != 0) {
            destination.y -= size * 0.125f;
            destination.height = size * 1.25f;
            DrawTexturePro(referenceCompressedTexture, (Rectangle){ 0, 0, 32, 40 }, destination,
                           (Vector2){ 0 }, 0.0f, WHITE);
        } else RpgStage_DrawReferenceObject(fileTexture, destination, WHITE);
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
        Vector2 objectPosition = RpgStage_GetWorldPositionForCell(stage, row, column);
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
        Vector2 folderPosition = RpgStage_GetWorldPositionForCell(stage, row, column);
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
    int column;
    int row;
    if (!RpgStage_GetWorldCellAtPosition(stage, position, &row, &column) ||
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
    if (blockType == RPG_BLOCK_DOOR_OPEN_MIDDLE || blockType == RPG_BLOCK_DOOR_OPEN_BOTTOM ||
        blockType == RPG_BLOCK_KEY_DOOR_OPEN_MIDDLE || blockType == RPG_BLOCK_KEY_DOOR_OPEN_BOTTOM) return false;
    if (RpgBlockInventory_IsReferenceObject(blockType)) return false;
    // 一方向床の衝突は、プレイヤーの下向き移動だけで個別に判定する。
    if (RpgBlockInventory_IsOneWayPlatform(blockType)) return false;
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

bool RpgStage_CheckSolidCollision(const RpgStage *stage, Rectangle bounds)
{
    return RpgStage_FindSolidCollisionCenter(stage, bounds, NULL);
}

static bool GetSpatialBlock(const RpgStage *stage, int stageColumn, int stageRow,
                            int *storageColumn, int *storageRow, Rectangle *stageCell)
{
    int gridX;
    int gridY;
    int localColumn;
    int localRow;
    int mapIndex;
    if (stage == NULL) return false;
    gridX = (int)floorf((float)stageColumn / RPG_STAGE_COLUMNS);
    gridY = -(int)floorf((float)stageRow / RPG_STAGE_ROWS);
    localColumn = stageColumn - gridX * RPG_STAGE_COLUMNS;
    localRow = stageRow + gridY * RPG_STAGE_ROWS;
    mapIndex = RpgStage_GetMapAtGrid(stage, gridX, gridY);
    if (mapIndex < 0 || localColumn < 0 || localColumn >= RPG_STAGE_COLUMNS ||
        localRow < 0 || localRow >= RPG_STAGE_ROWS) return false;
    if (storageColumn != NULL) *storageColumn = mapIndex * RPG_STAGE_COLUMNS + localColumn;
    if (storageRow != NULL) *storageRow = localRow;
    if (stageCell != NULL)
        *stageCell = (Rectangle){ stageColumn * RPG_STAGE_TILE_SIZE, stageRow * RPG_STAGE_TILE_SIZE,
                                  RPG_STAGE_TILE_SIZE, RPG_STAGE_TILE_SIZE };
    return true;
}

bool RpgStage_FindSolidCollisionCenter(const RpgStage *stage, Rectangle bounds, Vector2 *center)
{
    int firstColumn, lastColumn, firstRow, lastRow;
    if (stage == NULL) return false;
    firstColumn = (int)floorf(bounds.x / RPG_STAGE_TILE_SIZE);
    lastColumn = (int)floorf((bounds.x + bounds.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    firstRow = (int)floorf(bounds.y / RPG_STAGE_TILE_SIZE);
    lastRow = (int)floorf((bounds.y + bounds.height - 0.001f) / RPG_STAGE_TILE_SIZE);
    for (int row = firstRow; row <= lastRow; row++) for (int column = firstColumn; column <= lastColumn; column++) {
        int storageColumn, storageRow;
        Rectangle stageCell;
        if (!GetSpatialBlock(stage, column, row, &storageColumn, &storageRow, &stageCell)) continue;
        if (!RpgStage_CheckBlockCollision(bounds, stageCell, stage->blocks[storageRow][storageColumn])) continue;
        if (center != NULL)
            *center = (Vector2){ stageCell.x + stageCell.width * 0.5f,
                                 stageCell.y + stageCell.height * 0.5f };
        return true;
    }
    return false;
#if 0
    // 複数マスの特殊ブロックも構成マスごとに走査し、実際に衝突した1マスの中心を返す。
    int firstColumn = (int)floorf(bounds.x / RPG_STAGE_TILE_SIZE);
    int lastColumn = (int)floorf((bounds.x + bounds.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    int firstRow = (int)floorf(bounds.y / RPG_STAGE_TILE_SIZE);
    int lastRow = (int)floorf((bounds.y + bounds.height - 0.001f) / RPG_STAGE_TILE_SIZE);
    int mapFirstColumn, mapLastColumn;
    if (lastColumn < 0 || firstColumn >= RPG_STAGE_WORLD_COLUMNS ||
        lastRow < 0 || firstRow >= RPG_STAGE_ROWS) return false;
    if (!GetCollisionMapColumnRange(stage, bounds.x + bounds.width * 0.5f,
                                    &mapFirstColumn, &mapLastColumn)) return false;
    firstColumn = RpgStage_ClampIndex(firstColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    lastColumn = RpgStage_ClampIndex(lastColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    if (firstColumn < mapFirstColumn) firstColumn = mapFirstColumn;
    if (lastColumn > mapLastColumn) lastColumn = mapLastColumn;
    if (firstColumn > lastColumn) return false;
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
#endif
}

bool RpgStage_FindOneWayPlatformLanding(const RpgStage *stage, Rectangle previousBounds,
                                        Rectangle candidateBounds, float *landingY)
{
    float previousBottom;
    float candidateBottom;
    float nearestLandingY = 0.0f;
    bool found = false;
    if (stage == NULL) return false;
    previousBottom = previousBounds.y + previousBounds.height;
    candidateBottom = candidateBounds.y + candidateBounds.height;
    if (candidateBottom <= previousBottom) return false;
    int firstColumn = (int)floorf(candidateBounds.x / RPG_STAGE_TILE_SIZE);
    int lastColumn = (int)floorf((candidateBounds.x + candidateBounds.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    int firstRow = (int)floorf(previousBottom / RPG_STAGE_TILE_SIZE);
    int lastRow = (int)floorf((candidateBottom - 0.001f) / RPG_STAGE_TILE_SIZE);
    for (int row = firstRow; row <= lastRow; row++) for (int column = firstColumn; column <= lastColumn; column++) {
        int storageColumn, storageRow;
        Rectangle stageCell;
        if (!GetSpatialBlock(stage, column, row, &storageColumn, &storageRow, &stageCell) ||
            !RpgBlockInventory_IsOneWayPlatform(stage->blocks[storageRow][storageColumn]) ||
            previousBottom > stageCell.y + 0.001f || candidateBottom < stageCell.y ||
            candidateBounds.x >= stageCell.x + stageCell.width ||
            candidateBounds.x + candidateBounds.width <= stageCell.x) continue;
        if (!found || stageCell.y < nearestLandingY) {
            nearestLandingY = stageCell.y;
            found = true;
        }
    }
    if (found && landingY != NULL) *landingY = nearestLandingY;
    return found;
#if 0
    float previousBottom;
    float candidateBottom;
    int firstColumn, lastColumn, firstRow, lastRow;
    float nearestLandingY = 0.0f;
    bool found = false;

    if (stage == NULL) return false;
    previousBottom = previousBounds.y + previousBounds.height;
    candidateBottom = candidateBounds.y + candidateBounds.height;
    /* 上から下へ移動したフレームだけを対象にして、下・横・上向き移動はすり抜けさせる。 */
    if (candidateBottom <= previousBottom) return false;

    firstColumn = (int)floorf(candidateBounds.x / RPG_STAGE_TILE_SIZE);
    lastColumn = (int)floorf((candidateBounds.x + candidateBounds.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    firstRow = (int)floorf(previousBottom / RPG_STAGE_TILE_SIZE);
    lastRow = (int)floorf((candidateBottom - 0.001f) / RPG_STAGE_TILE_SIZE);
    int mapFirstColumn, mapLastColumn;
    if (lastColumn < 0 || firstColumn >= RPG_STAGE_WORLD_COLUMNS ||
        lastRow < 0 || firstRow >= RPG_STAGE_ROWS) return false;
    if (!GetCollisionMapColumnRange(stage, candidateBounds.x + candidateBounds.width * 0.5f,
                                    &mapFirstColumn, &mapLastColumn)) return false;
    firstColumn = RpgStage_ClampIndex(firstColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    lastColumn = RpgStage_ClampIndex(lastColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    if (firstColumn < mapFirstColumn) firstColumn = mapFirstColumn;
    if (lastColumn > mapLastColumn) lastColumn = mapLastColumn;
    if (firstColumn > lastColumn) return false;
    firstRow = RpgStage_ClampIndex(firstRow, 0, RPG_STAGE_ROWS - 1);
    lastRow = RpgStage_ClampIndex(lastRow, 0, RPG_STAGE_ROWS - 1);

    for (int row = firstRow; row <= lastRow; row++) for (int column = firstColumn; column <= lastColumn; column++) {
        float platformTop = (float)(row * RPG_STAGE_TILE_SIZE);
        if (!RpgBlockInventory_IsOneWayPlatform(stage->blocks[row][column]) ||
            previousBottom > platformTop + 0.001f || candidateBottom < platformTop ||
            candidateBounds.x >= (float)((column + 1) * RPG_STAGE_TILE_SIZE) ||
            candidateBounds.x + candidateBounds.width <= (float)(column * RPG_STAGE_TILE_SIZE)) continue;
        if (!found || platformTop < nearestLandingY) {
            nearestLandingY = platformTop;
            found = true;
        }
    }
    if (found && landingY != NULL) *landingY = nearestLandingY;
#endif
}

bool RpgStage_FindEffectRootCell(const RpgStage *stage, int row, int column, int *rootRow,
                                 int *rootColumn, const RpgEffectShape **shapeResult)
{
    if (stage == NULL || rootRow == NULL || rootColumn == NULL || row < 0 || row >= RPG_STAGE_ROWS ||
        column < 0 || column >= RPG_STAGE_WORLD_COLUMNS) return false;
    const RpgEffectShape *shape = RpgBlockInventory_GetEffectShape(stage->blocks[row][column]);
    if (shape == NULL) return false;
    for (int cellIndex = 0; cellIndex < shape->cellCount; cellIndex++) {
        const RpgEffectShapeCell *cell = &shape->cells[cellIndex];
        if (cell->blockType != stage->blocks[row][column]) continue;
        int candidateRow = row - cell->offsetY;
        int candidateColumn = column - cell->offsetX;
        bool isComplete = true;
        for (int shapeIndex = 0; shapeIndex < shape->cellCount; shapeIndex++) {
            const RpgEffectShapeCell *shapeCell = &shape->cells[shapeIndex];
            int checkRow = candidateRow + shapeCell->offsetY;
            int checkColumn = candidateColumn + shapeCell->offsetX;
            if (checkRow < 0 || checkRow >= RPG_STAGE_ROWS || checkColumn < 0 ||
                checkColumn >= RPG_STAGE_WORLD_COLUMNS ||
                stage->blocks[checkRow][checkColumn] != shapeCell->blockType) {
                isComplete = false;
                break;
            }
        }
        if (!isComplete) continue;
        *rootRow = candidateRow;
        *rootColumn = candidateColumn;
        if (shapeResult != NULL) *shapeResult = shape;
        return true;
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
    Rectangle bounds = { center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f };
    if (stage == NULL) return false;
    int firstColumn = (int)floorf(bounds.x / RPG_STAGE_TILE_SIZE);
    int lastColumn = (int)floorf((bounds.x + bounds.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    int firstRow = (int)floorf(bounds.y / RPG_STAGE_TILE_SIZE);
    int lastRow = (int)floorf((bounds.y + bounds.height - 0.001f) / RPG_STAGE_TILE_SIZE);
    for (int row = firstRow; row <= lastRow; row++) for (int column = firstColumn; column <= lastColumn; column++) {
        int storageColumn, storageRow;
        Rectangle stageCell;
        Rectangle solidParts[2];
        if (!GetSpatialBlock(stage, column, row, &storageColumn, &storageRow, &stageCell)) continue;
        int blockType = stage->blocks[storageRow][storageColumn];
        int solidPartCount = RpgStage_GetHoleSolidParts(stageCell, blockType, solidParts);
        bool hit = false;
        if (solidPartCount > 0) {
            for (int partIndex = 0; partIndex < solidPartCount; partIndex++)
                if (CheckCollisionCircleRec(center, radius, solidParts[partIndex])) { hit = true; break; }
        } else if (RpgStage_IsSolidBlock(blockType)) hit = CheckCollisionCircleRec(center, radius, stageCell);
        if (!hit) continue;
        if (collisionCenter != NULL)
            *collisionCenter = (Vector2){ stageCell.x + stageCell.width * 0.5f,
                                         stageCell.y + stageCell.height * 0.5f };
        return true;
    }
    return false;
#if 0
    // 弾の半径まで含めて走査し、穴付きブロックも実際の壁部分だけと円で判定する。
    Rectangle range = { center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f };
    int firstColumn = (int)floorf(range.x / RPG_STAGE_TILE_SIZE);
    int lastColumn = (int)floorf((range.x + range.width - 0.001f) / RPG_STAGE_TILE_SIZE);
    int firstRow = (int)floorf(range.y / RPG_STAGE_TILE_SIZE);
    int lastRow = (int)floorf((range.y + range.height - 0.001f) / RPG_STAGE_TILE_SIZE);
    int mapFirstColumn, mapLastColumn;
    if (lastColumn < 0 || firstColumn >= RPG_STAGE_WORLD_COLUMNS ||
        lastRow < 0 || firstRow >= RPG_STAGE_ROWS) return false;
    if (!GetCollisionMapColumnRange(stage, center.x, &mapFirstColumn, &mapLastColumn)) return false;
    firstColumn = RpgStage_ClampIndex(firstColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    lastColumn = RpgStage_ClampIndex(lastColumn, 0, RPG_STAGE_WORLD_COLUMNS - 1);
    if (firstColumn < mapFirstColumn) firstColumn = mapFirstColumn;
    if (lastColumn > mapLastColumn) lastColumn = mapLastColumn;
    if (firstColumn > lastColumn) return false;
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
#endif
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
    if (blockType == RPG_BLOCK_EFFECT_MAGNET_OFF) return (Color){ 65, 72, 82, 255 };
    if (blockType == RPG_BLOCK_EFFECT_MAGNET_ON) return (Color){ 37, 78, 112, 255 };
    if (blockType == RPG_BLOCK_METAL) return (Color){ 112, 123, 136, 255 };
    if (blockType == RPG_BLOCK_PUSH_BLOCK) return (Color){ 132, 88, 52, 255 };
    if (blockType == RPG_BLOCK_DOOR_OPEN_TOP) blockType = RPG_BLOCK_DOOR_CLOSED_TOP;
    if (blockType == RPG_BLOCK_KEY_DOOR_OPEN_TOP) blockType = RPG_BLOCK_KEY_DOOR_CLOSED_TOP;
    if (blockType >= RPG_BLOCK_KEY_DOOR_CLOSED_TOP && blockType <= RPG_BLOCK_KEY_DOOR_OPEN_BOTTOM)
        return (Color){ 68, 48, 105, 255 };
    /* The Earth palette is one visual material, even when its placement slots
       use different basic block IDs. */
    if (blockType >= 1 && blockType <= 10) return colors[1];
    return blockType >= 1 && blockType <= RPG_BLOCK_DOOR_OPEN_BOTTOM ? colors[blockType] :
           blockType == RPG_BLOCK_EFFECT_BUTTON ? (Color){ 72, 84, 104, 255 } : colors[1];
}

void RpgStage_SetGroundTexture(Texture2D texture)
{
    groundTexture = texture;
}

void RpgStage_SetGroundAppearance(float hue, float saturation, float lightness)
{
    groundHue = Clamp(hue, 0.0f, 1.0f);
    groundSaturation = Clamp(saturation, 0.0f, 1.0f);
    groundLightness = Clamp(lightness, 0.0f, 1.0f);
}

void RpgStage_DrawEffectSymbol(Rectangle cell, int blockType)
{
    const float center = RPG_STAGE_TILE_SIZE * 0.5f;
    if (blockType == RPG_BLOCK_EFFECT_MAGNET_OFF || blockType == RPG_BLOCK_EFFECT_MAGNET_ON)
        return;
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
    if (blockType == RPG_BLOCK_KEY_DOOR_CLOSED_TOP) {
        DrawRectangleLinesEx((Rectangle){ cell.x + 8.0f, cell.y + 3.0f, 16.0f,
                                           RPG_STAGE_TILE_SIZE * 3.0f - 6.0f }, 2.0f, VIOLET);
        DrawCircle((int)(cell.x + center), (int)(cell.y + 12.0f), 6.0f, SKYBLUE);
        DrawRectangle((int)(cell.x + center - 2.0f), (int)(cell.y + 12.0f), 4, 10, SKYBLUE);
    }
    if (blockType == RPG_BLOCK_KEY_DOOR_OPEN_TOP) {
        DrawCircleV((Vector2){ cell.x + center, cell.y + 10.0f }, 9.0f, Fade(SKYBLUE, 0.35f));
        DrawCircleV((Vector2){ cell.x + center, cell.y + 10.0f }, 4.0f, SKYBLUE);
        DrawLineEx((Vector2){ cell.x + 5.0f, cell.y + 10.0f },
                   (Vector2){ cell.x + RPG_STAGE_TILE_SIZE - 5.0f, cell.y + 10.0f }, 2.0f, RAYWHITE);
    }
    if (blockType == RPG_BLOCK_EFFECT_BUTTON) {
        DrawRectangleRec((Rectangle){ cell.x + 5.0f, cell.y + 20.0f, 22.0f, 8.0f }, DARKGRAY);
        DrawRectangleLinesEx((Rectangle){ cell.x + 5.0f, cell.y + 20.0f, 22.0f, 8.0f }, 2.0f, RAYWHITE);
        DrawCircle((int)(cell.x + center), (int)(cell.y + 19.0f), 5.0f, RED);
    }
    if (blockType == RPG_BLOCK_EFFECT_MAGNET_OFF || blockType == RPG_BLOCK_EFFECT_MAGNET_ON) {
        bool active = blockType == RPG_BLOCK_EFFECT_MAGNET_ON;
        Color leftPole = active ? RED : GRAY;
        Color rightPole = active ? SKYBLUE : GRAY;
        Color core = active ? YELLOW : LIGHTGRAY;
        if (active) DrawCircleV((Vector2){ cell.x + center, cell.y + center }, 15.0f, Fade(SKYBLUE, 0.20f));
        DrawLineEx((Vector2){ cell.x + 9.0f, cell.y + 8.0f },
                   (Vector2){ cell.x + 9.0f, cell.y + 22.0f }, 5.0f, leftPole);
        DrawLineEx((Vector2){ cell.x + 23.0f, cell.y + 8.0f },
                   (Vector2){ cell.x + 23.0f, cell.y + 22.0f }, 5.0f, rightPole);
        DrawLineEx((Vector2){ cell.x + 9.0f, cell.y + 22.0f },
                   (Vector2){ cell.x + 23.0f, cell.y + 22.0f }, 5.0f, core);
        DrawCircle((int)(cell.x + 9.0f), (int)(cell.y + 7.0f), 3.0f, RAYWHITE);
        DrawCircle((int)(cell.x + 23.0f), (int)(cell.y + 7.0f), 3.0f, RAYWHITE);
    }
    if (blockType == RPG_BLOCK_METAL) {
        DrawRectangleLinesEx((Rectangle){ cell.x + 4.0f, cell.y + 4.0f, 24.0f, 24.0f }, 2.0f, LIGHTGRAY);
        DrawCircle((int)(cell.x + 10.0f), (int)(cell.y + 10.0f), 2.0f, DARKGRAY);
        DrawCircle((int)(cell.x + 22.0f), (int)(cell.y + 22.0f), 2.0f, DARKGRAY);
    }
    if (blockType == RPG_BLOCK_PUSH_BLOCK) {
        DrawRectangleLinesEx((Rectangle){ cell.x + 3.0f, cell.y + 3.0f, 26.0f, 26.0f }, 2.0f, GOLD);
        DrawLineEx((Vector2){ cell.x + 7.0f, cell.y + 16.0f },
                   (Vector2){ cell.x + 25.0f, cell.y + 16.0f }, 2.0f, RAYWHITE);
        DrawTriangle((Vector2){ cell.x + 6.0f, cell.y + 16.0f },
                     (Vector2){ cell.x + 11.0f, cell.y + 12.0f },
                     (Vector2){ cell.x + 11.0f, cell.y + 20.0f }, RAYWHITE);
        DrawTriangle((Vector2){ cell.x + 26.0f, cell.y + 16.0f },
                     (Vector2){ cell.x + 21.0f, cell.y + 12.0f },
                     (Vector2){ cell.x + 21.0f, cell.y + 20.0f }, RAYWHITE);
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

static Color RpgStage_AdjustGroundColor(Color color)
{
    float red = (float)color.r / 255.0f;
    float green = (float)color.g / 255.0f;
    float blue = (float)color.b / 255.0f;
    float high = fmaxf(red, fmaxf(green, blue));
    float low = fminf(red, fminf(green, blue));
    float difference = high - low;
    float lightness = (high + low) * 0.5f;
    float saturation = 0.0f;
    float hue = 0.0f;
    if (difference > 0.00001f) {
        saturation = difference / (1.0f - fabsf(2.0f * lightness - 1.0f));
        if (high == red) hue = fmodf((green - blue) / difference, 6.0f);
        else if (high == green) hue = (blue - red) / difference + 2.0f;
        else hue = (red - green) / difference + 4.0f;
        hue /= 6.0f;
    }
    hue = fmodf(hue + groundHue - 0.50f + 1.0f, 1.0f);
    saturation = Clamp(saturation * groundSaturation * 2.0f, 0.0f, 1.0f);
    lightness = Clamp(lightness * groundLightness * 2.0f, 0.0f, 1.0f);
    if (saturation < 0.00001f) return (Color){ (unsigned char)(lightness * 255.0f + 0.5f),
                                                (unsigned char)(lightness * 255.0f + 0.5f),
                                                (unsigned char)(lightness * 255.0f + 0.5f), color.a };
    float q = lightness < 0.5f ? lightness * (1.0f + saturation) :
                                  lightness + saturation - lightness * saturation;
    float p = 2.0f * lightness - q;
    float components[3] = { hue + 1.0f / 3.0f, hue, hue - 1.0f / 3.0f };
    float rgb[3];
    for (int index = 0; index < 3; index++) {
        float t = components[index];
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        rgb[index] = t < 1.0f / 6.0f ? p + (q - p) * 6.0f * t :
                     t < 0.5f ? q :
                     t < 2.0f / 3.0f ? p + (q - p) * (2.0f / 3.0f - t) * 6.0f : p;
    }
    return (Color){ (unsigned char)(rgb[0] * 255.0f + 0.5f),
                    (unsigned char)(rgb[1] * 255.0f + 0.5f),
                    (unsigned char)(rgb[2] * 255.0f + 0.5f), color.a };
}

static bool RpgStage_DrawGroundTexturePart(Rectangle cell, Rectangle part, float brightness)
{
    if (groundTexture.id == 0 || cell.width <= 0.0f || cell.height <= 0.0f ||
        part.width <= 0.0f || part.height <= 0.0f) return false;
    Rectangle source = {
        (part.x - cell.x) * (float)groundTexture.width / cell.width,
        (part.y - cell.y) * (float)groundTexture.height / cell.height,
        part.width * (float)groundTexture.width / cell.width,
        part.height * (float)groundTexture.height / cell.height
    };
    bool needsAdjustment = fabsf(groundHue - 0.50f) > 0.001f ||
                           fabsf(groundSaturation - 0.50f) > 0.001f ||
                           fabsf(groundLightness - 0.50f) > 0.001f;
    if (needsAdjustment && groundAdjustmentShader.id == 0) {
        groundAdjustmentShader = LoadShaderFromMemory(NULL, groundAdjustmentFragmentShader);
        if (groundAdjustmentShader.id != 0) {
            groundHueLocation = GetShaderLocation(groundAdjustmentShader, "groundHue");
            groundSaturationLocation = GetShaderLocation(groundAdjustmentShader, "groundSaturation");
            groundLightnessLocation = GetShaderLocation(groundAdjustmentShader, "groundLightness");
        }
    }
    if (needsAdjustment && groundAdjustmentShader.id != 0) {
        if (groundHueLocation >= 0)
            SetShaderValue(groundAdjustmentShader, groundHueLocation, &groundHue, SHADER_UNIFORM_FLOAT);
        if (groundSaturationLocation >= 0)
            SetShaderValue(groundAdjustmentShader, groundSaturationLocation, &groundSaturation, SHADER_UNIFORM_FLOAT);
        if (groundLightnessLocation >= 0)
            SetShaderValue(groundAdjustmentShader, groundLightnessLocation, &groundLightness, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(groundAdjustmentShader);
    }
    DrawTexturePro(groundTexture, source, part, (Vector2){ 0.0f, 0.0f }, 0.0f,
                   ApplyBlockBrightness(WHITE, brightness));
    if (needsAdjustment && groundAdjustmentShader.id != 0) EndShaderMode();
    return true;
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
    bool isGroundMaterial = (blockType >= 1 && blockType <= 10) ||
                            blockType == RPG_BLOCK_HOLE_VERTICAL ||
                            blockType == RPG_BLOCK_HOLE_HORIZONTAL ||
                            RpgBlockInventory_IsOneWayPlatform(blockType);
    Color baseColor = RpgStage_GetBlockColor(blockType);
    if (isGroundMaterial) baseColor = RpgStage_AdjustGroundColor(baseColor);
    Color color = ApplyBlockBrightness(baseColor, brightness);
    Rectangle solidParts[2];
    int solidPartCount = RpgStage_GetHoleSolidParts(cell, blockType, solidParts);
    // 穴ブロックは、衝突判定と同じ実体部分だけを描画する。
    if (blockType == RPG_BLOCK_PUSH_BLOCK &&
        RpgGimicSprites_Draw(RPG_GIMIC_SPRITE_BOX, cell, ApplyBlockBrightness(WHITE, brightness))) {
        return;
    } else if (blockType == RPG_BLOCK_METAL &&
               RpgGimicSprites_Draw(RPG_GIMIC_SPRITE_STEEL, cell, ApplyBlockBrightness(WHITE, brightness))) {
        return;
    } else if ((blockType == RPG_BLOCK_EFFECT_MAGNET_OFF || blockType == RPG_BLOCK_EFFECT_MAGNET_ON) &&
               RpgGimicSprites_Draw(RPG_GIMIC_SPRITE_MAGNET, cell,
                   ApplyBlockBrightness(blockType == RPG_BLOCK_EFFECT_MAGNET_ON ? WHITE : LIGHTGRAY, brightness))) {
        return;
    } else if (blockType >= 1 && blockType <= 10 && groundTexture.id != 0) {
        RpgStage_DrawGroundTexturePart(cell, cell, brightness);
    } else if (solidPartCount > 0) {
        for (int index = 0; index < solidPartCount; index++)
            if (!RpgStage_DrawGroundTexturePart(cell, solidParts[index], brightness))
                DrawRectangleRec(solidParts[index], color);
    } else if (RpgBlockInventory_IsOneWayPlatform(blockType)) {
        /* 1マスを埋めず、上辺だけを草と土の薄い床として描画する。 */
        Rectangle floorPart = { cell.x, cell.y, cell.width, 7.0f };
        if (!RpgStage_DrawGroundTexturePart(cell, floorPart, brightness)) {
            Color grass = ApplyBlockBrightness(RpgStage_AdjustGroundColor((Color){ 91, 130, 66, 255 }), brightness);
            DrawRectangleRec((Rectangle){ cell.x, cell.y, cell.width, 3.0f }, grass);
            DrawRectangleRec((Rectangle){ cell.x, cell.y + 3.0f, cell.width, 4.0f }, color);
        }
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
#define RPG_REFERENCE_COMPRESSION_DURATION 0.48f
