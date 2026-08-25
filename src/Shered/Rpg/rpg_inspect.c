// 依存する自プロジェクト内ファイル: rpg_inspect.h
#include "rpg_inspect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void SetDefaultDialogue(RpgInspect *inspect, int index, const char *speaker, const char *text)
{
    inspect->functions[index].type = RPG_INSPECT_DIALOGUE;
    snprintf(inspect->functions[index].title, RPG_INSPECT_TITLE_LENGTH, "Dialogue%d", index + 1);
    inspect->functions[index].dialogue.lineCount = 1;
    strcpy(inspect->functions[index].dialogue.speakers[0], speaker);
    strcpy(inspect->functions[index].dialogue.lines[0], text);
}

RpgInspect RpgInspect_Default(const char *speaker, const char *text)
{
    RpgInspect inspect = { .enabled = false, .functionCount = 1 };
    SetDefaultDialogue(&inspect, 0, speaker, text);
    return inspect;
}

float RpgInspect_EaseMoveProgress(RpgInspectMoveEasing easing, float progress)
{
    if (progress <= 0.0f) return 0.0f;
    if (progress >= 1.0f) return 1.0f;
    switch (easing) {
    case RPG_INSPECT_EASING_QUADRATIC_IN:
        return progress * progress;
    case RPG_INSPECT_EASING_QUADRATIC_OUT:
        return 1.0f - (1.0f - progress) * (1.0f - progress);
    case RPG_INSPECT_EASING_QUADRATIC_IN_OUT:
        return progress < 0.5f ? 2.0f * progress * progress :
               1.0f - (-2.0f * progress + 2.0f) * (-2.0f * progress + 2.0f) * 0.5f;
    case RPG_INSPECT_EASING_BOUNCE_OUT: {
        const float n = 7.5625f, d = 2.75f;
        if (progress < 1.0f / d) return n * progress * progress;
        if (progress < 2.0f / d) { progress -= 1.5f / d; return n * progress * progress + 0.75f; }
        if (progress < 2.5f / d) { progress -= 2.25f / d; return n * progress * progress + 0.9375f; }
        progress -= 2.625f / d;
        return n * progress * progress + 0.984375f;
    }
    case RPG_INSPECT_EASING_LINEAR:
    default:
        return progress;
    }
}

const char *RpgInspect_MoveEasingName(RpgInspectMoveEasing easing)
{
    static const char *names[RPG_INSPECT_EASING_COUNT] = {
        "Linear", "Quadratic in", "Quadratic out", "Quadratic in/out", "Bounce out"
    };
    return easing >= RPG_INSPECT_EASING_LINEAR && easing < RPG_INSPECT_EASING_COUNT ?
           names[easing] : names[RPG_INSPECT_EASING_LINEAR];
}

const char *RpgInspect_MoveAxisName(RpgInspectMoveAxis axis)
{
    static const char *const names[RPG_INSPECT_MOVE_AXIS_COUNT] = { "X only", "Y only", "X and Y" };
    return axis >= RPG_INSPECT_MOVE_AXIS_X && axis < RPG_INSPECT_MOVE_AXIS_COUNT ? names[axis] : names[0];
}

bool RpgInspect_MoveAxisHasX(RpgInspectMoveAxis axis)
{
    return axis == RPG_INSPECT_MOVE_AXIS_X || axis == RPG_INSPECT_MOVE_AXIS_XY;
}

bool RpgInspect_MoveAxisHasY(RpgInspectMoveAxis axis)
{
    return axis == RPG_INSPECT_MOVE_AXIS_Y || axis == RPG_INSPECT_MOVE_AXIS_XY;
}

static bool LoadDialogueLines(FILE *file, RpgDialogue *dialogue, int count)
{
    char buffer[RPG_DIALOGUE_SPEAKER_LENGTH + RPG_DIALOGUE_LINE_LENGTH + 2];
    dialogue->lineCount = 0;
    for (int line = 0; line < count && line < RPG_DIALOGUE_MAX_LINES; line++) {
        if (fgets(buffer, sizeof(buffer), file) == NULL) break;
        char *tab = strchr(buffer, '\t');
        if (tab == NULL) continue;
        *tab++ = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        tab[strcspn(tab, "\r\n")] = '\0';
        strcpy(dialogue->speakers[dialogue->lineCount], buffer);
        strcpy(dialogue->lines[dialogue->lineCount++], tab);
    }
    return dialogue->lineCount > 0;
}

bool RpgInspect_LoadStream(FILE *file, RpgInspect *inspect)
{
    int enabled = 0, count = 0;
    if (file == NULL || inspect == NULL || fscanf(file, "%d %d\n", &enabled, &count) != 2) return false;
    inspect->enabled = enabled != 0;
    inspect->functionCount = 0;
    for (int sourceIndex = 0; sourceIndex < count && inspect->functionCount < RPG_INSPECT_MAX_FUNCTIONS; sourceIndex++) {
        char buffer[RPG_DIALOGUE_SPEAKER_LENGTH + RPG_DIALOGUE_LINE_LENGTH + 2];
        if (fgets(buffer, sizeof(buffer), file) == NULL) break;
        int index = inspect->functionCount;
        RpgInspectFunction *function = &inspect->functions[index];
        RpgDialogue *dialogue = &function->dialogue;
        if (buffer[1] == '\t' && (buffer[0] == 'D' || buffer[0] == 'M' || buffer[0] == 'W' || buffer[0] == 'L')) {
            char *type = strtok(buffer, "\t\r\n");
            char *title = strtok(NULL, "\t\r\n");
            char *value = strtok(NULL, "\t\r\n");
            if (title == NULL) title = "Function";
            strncpy(function->title, title, RPG_INSPECT_TITLE_LENGTH - 1);
            function->title[RPG_INSPECT_TITLE_LENGTH - 1] = '\0';
            if (type[0] == 'M') {
                int target = 0, easing = RPG_INSPECT_EASING_LINEAR, axis = RPG_INSPECT_MOVE_AXIS_X, snapToGrid = 1;
                unsigned int imageObjectId = 0;
                float destinationX = 0.0f, destinationY = 0.0f, duration = 1.0f, nextFunctionDelay = 0.0f;
                int walkAnimationEnabled = 1;
                float walkAnimationSpeed = 1.0f;
                /* 先頭5項目は旧形式と同じに保ち、後ろの軸・Y・スナップを任意で読む。 */
                int valuesRead = value != NULL ?
                    sscanf(value, "%d %f %f %d %u %f %d %d %f %d %f", &target, &destinationX, &duration,
                           &easing, &imageObjectId, &destinationY, &axis, &snapToGrid, &nextFunctionDelay,
                           &walkAnimationEnabled, &walkAnimationSpeed) : 0;
                /* 過去形式に存在しない遷移待ちは、現在の既定どおり即時遷移として読み込む。 */
                if (valuesRead < 9) nextFunctionDelay = 0.0f;
                if (valuesRead < 10) walkAnimationEnabled = 1;
                if (valuesRead < 11) walkAnimationSpeed = 1.0f;
                function->type = RPG_INSPECT_MOVE;
                function->move = (RpgInspectMove){ .target = (RpgInspectMoveTarget)target,
                                                    .destinationX = destinationX, .destinationY = destinationY,
                                                    .duration = duration, .nextFunctionDelay = nextFunctionDelay,
                                                    .walkAnimationEnabled = walkAnimationEnabled != 0,
                                                    .walkAnimationSpeed = walkAnimationSpeed,
                                                    .easing = (RpgInspectMoveEasing)easing,
                                                    .axis = (RpgInspectMoveAxis)axis, .snapToGrid = snapToGrid != 0,
                                                    .targetImageObjectId = imageObjectId };
                if (function->move.target < RPG_INSPECT_MOVE_PLAYER ||
                    function->move.target > RPG_INSPECT_MOVE_IMAGE_OBJECT)
                    function->move.target = RPG_INSPECT_MOVE_PLAYER;
                if (function->move.duration < 0.1f) function->move.duration = 1.0f;
                if (function->move.nextFunctionDelay < 0.0f) function->move.nextFunctionDelay = 0.0f;
                if (function->move.walkAnimationSpeed < 0.1f) function->move.walkAnimationSpeed = 1.0f;
                if (function->move.walkAnimationSpeed > 8.0f) function->move.walkAnimationSpeed = 8.0f;
                if (function->move.easing < RPG_INSPECT_EASING_LINEAR ||
                    function->move.easing >= RPG_INSPECT_EASING_COUNT)
                    function->move.easing = RPG_INSPECT_EASING_LINEAR;
                if (function->move.axis < RPG_INSPECT_MOVE_AXIS_X ||
                    function->move.axis >= RPG_INSPECT_MOVE_AXIS_COUNT)
                    function->move.axis = RPG_INSPECT_MOVE_AXIS_X;
                /* Move機能の終点は常にマスへ揃える。旧設定の切替値は読んでも使用しない。 */
                function->move.snapToGrid = true;
            } else if (type[0] == 'W') {
                float duration = value != NULL ? (float)atof(value) : 1.0f;
                function->type = RPG_INSPECT_WAIT;
                function->wait.duration = duration < 0.0f ? 0.0f : duration;
            } else if (type[0] == 'L') {
                unsigned int imageObjectId = 0;
                int layer = 1;
                if (value != NULL) sscanf(value, "%u %d", &imageObjectId, &layer);
                function->type = RPG_INSPECT_LAYER_CHANGE;
                function->layerChange.targetImageObjectId = imageObjectId;
                function->layerChange.layer = layer < 0 ? 0 : (layer > 2 ? 2 : layer);
            } else {
                int lineCount = value != NULL ? atoi(value) : 1;
                function->type = RPG_INSPECT_DIALOGUE;
                if (!LoadDialogueLines(file, dialogue, lineCount)) SetDefaultDialogue(inspect, index, "Inspect", "");
            }
        } else {
            // 旧形式のDialogue設定を読み込み、新形式のDialogueとして扱う。
            char *tab = strchr(buffer, '\t');
            if (tab != NULL) {
                *tab++ = '\0';
                char *end = NULL;
                long lineCount = strtol(tab, &end, 10);
                if (end != tab && (*end == '\r' || *end == '\n' || *end == '\0')) {
                    buffer[strcspn(buffer, "\r\n")] = '\0';
                    strncpy(function->title, buffer, RPG_INSPECT_TITLE_LENGTH - 1);
                    function->title[RPG_INSPECT_TITLE_LENGTH - 1] = '\0';
                    function->type = RPG_INSPECT_DIALOGUE;
                    if (!LoadDialogueLines(file, dialogue, (int)lineCount)) SetDefaultDialogue(inspect, index, "Inspect", "");
                } else {
                    SetDefaultDialogue(inspect, index, buffer, tab);
                    dialogue->lines[0][strcspn(dialogue->lines[0], "\r\n")] = '\0';
                }
            } else {
                SetDefaultDialogue(inspect, index, "Inspect", "");
            }
        }
        inspect->functionCount++;
    }
    return inspect->functionCount > 0;
}

bool RpgInspect_SaveStream(FILE *file, const RpgInspect *inspect)
{
    if (!file || !inspect) return false;
    fprintf(file, "%d %d\n", inspect->enabled ? 1 : 0, inspect->functionCount);
    for (int index = 0; index < inspect->functionCount; index++) {
        const RpgInspectFunction *function = &inspect->functions[index];
        if (function->type == RPG_INSPECT_MOVE) {
            const RpgInspectMove *move = &function->move;
            fprintf(file, "M\t%s\t%d %.2f %.2f %d %u %.2f %d %d %.2f %d %.2f\n", function->title, move->target,
                    move->destinationX, move->duration, move->easing, move->targetImageObjectId,
                    move->destinationY, move->axis, 1, move->nextFunctionDelay,
                    move->walkAnimationEnabled ? 1 : 0, move->walkAnimationSpeed);
        } else if (function->type == RPG_INSPECT_WAIT) {
            fprintf(file, "W\t%s\t%.2f\n", function->title, function->wait.duration);
        } else if (function->type == RPG_INSPECT_LAYER_CHANGE) {
            fprintf(file, "L\t%s\t%u %d\n", function->title, function->layerChange.targetImageObjectId,
                    function->layerChange.layer);
        } else {
            const RpgDialogue *dialogue = &function->dialogue;
            fprintf(file, "D\t%s\t%d\n", function->title, dialogue->lineCount);
            for (int line = 0; line < dialogue->lineCount; line++)
                fprintf(file, "%s\t%s\n", dialogue->speakers[line], dialogue->lines[line]);
        }
    }
    return !ferror(file);
}

bool RpgInspect_Load(const char *filePath, RpgInspect *inspect)
{
    FILE *file = fopen(filePath, "r");
    bool loaded = RpgInspect_LoadStream(file, inspect);
    if (file != NULL) fclose(file);
    return loaded;
}

bool RpgInspect_Save(const char *filePath, const RpgInspect *inspect)
{
    FILE *file = fopen(filePath, "w");
    bool saved = RpgInspect_SaveStream(file, inspect);
    if (file != NULL && fclose(file) != 0) saved = false;
    return saved;
}
// 役割: 調べる機能の会話・移動 Function 列を保存・編集する。
