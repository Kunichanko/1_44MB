#include "rpg_key_door_trace.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static FILE *traceFile = NULL;

static void OpenTraceFile(const wchar_t *mode)
{
    wchar_t path[MAX_PATH];
    wchar_t *separator;
    if (traceFile != NULL) return;
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) return;
    separator = wcsrchr(path, L'\\');
    if (separator == NULL) return;
    wcscpy_s(separator + 1, MAX_PATH - (size_t)(separator + 1 - path),
             L"key_door_trace.log");
    traceFile = _wfopen(path, mode);
}

void RpgKeyDoorTrace_Reset(void)
{
    if (traceFile != NULL) {
        fclose(traceFile);
        traceFile = NULL;
    }
    OpenTraceFile(L"wb");
    if (traceFile != NULL) {
        fputs("key-door trace: runtime reset\n", traceFile);
        fflush(traceFile);
    }
}

void RpgKeyDoorTrace_Log(const char *format, ...)
{
    va_list arguments;
    OpenTraceFile(L"ab");
    if (traceFile == NULL || format == NULL) return;
    va_start(arguments, format);
    vfprintf(traceFile, format, arguments);
    va_end(arguments);
    fputc('\n', traceFile);
    fflush(traceFile);
}
#else
void RpgKeyDoorTrace_Reset(void) {}
void RpgKeyDoorTrace_Log(const char *format, ...) { (void)format; }
#endif
