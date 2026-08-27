#include "logger.h"

#include <shlobj.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define LOG_ROTATE_BYTES (2ULL * 1024ULL * 1024ULL)

static CRITICAL_SECTION g_log_lock;
static BOOL g_log_initialized = FALSE;
static wchar_t g_log_path[MAX_PATH];

static void RotateLogIfNeeded(void) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExW(g_log_path, GetFileExInfoStandard, &attributes)) return;
    ULONGLONG bytes = ((ULONGLONG)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
    if (bytes < LOG_ROTATE_BYTES) return;
    wchar_t old_path[MAX_PATH];
    if (SUCCEEDED(StringCchPrintfW(old_path, MAX_PATH, L"%s.old", g_log_path))) {
        MoveFileExW(g_log_path, old_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
}

BOOL Logger_Init(const wchar_t *local_app_dir) {
    if (g_log_initialized || !local_app_dir || !*local_app_dir) return g_log_initialized;
    InitializeCriticalSection(&g_log_lock);
    wchar_t log_dir[MAX_PATH];
    if (FAILED(StringCchPrintfW(log_dir, MAX_PATH, L"%s\\logs", local_app_dir)) ||
        (SHCreateDirectoryExW(NULL, log_dir, NULL) != ERROR_SUCCESS &&
         GetFileAttributesW(log_dir) == INVALID_FILE_ATTRIBUTES) ||
        FAILED(StringCchPrintfW(g_log_path, MAX_PATH, L"%s\\Febius.log", log_dir))) {
        DeleteCriticalSection(&g_log_lock);
        g_log_path[0] = 0;
        return FALSE;
    }
    RotateLogIfNeeded();
    g_log_initialized = TRUE;
    Logger_Write(L"app", L"로그를 시작했습니다.");
    return TRUE;
}

void Logger_Write(const wchar_t *category, const wchar_t *message) {
    if (!g_log_initialized || !g_log_path[0]) return;
    if (!category) category = L"app";
    if (!message) message = L"";

    SYSTEMTIME now;
    GetLocalTime(&now);
    size_t capacity = wcslen(category) + wcslen(message) + 96;
    wchar_t *line = (wchar_t *)calloc(capacity, sizeof(wchar_t));
    if (!line) return;
    if (FAILED(StringCchPrintfW(line, capacity,
            L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
            now.wSecond, now.wMilliseconds, category, message))) {
        free(line);
        return;
    }

    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
    char *utf8 = bytes > 0 ? (char *)malloc((size_t)bytes) : NULL;
    if (!utf8) {
        free(line);
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, bytes, NULL, NULL);

    EnterCriticalSection(&g_log_lock);
    FILE *file = _wfopen(g_log_path, L"ab");
    if (file) {
        fwrite(utf8, 1, (size_t)bytes - 1, file);
        fclose(file);
    }
    LeaveCriticalSection(&g_log_lock);
    free(utf8);
    free(line);
}

const wchar_t *Logger_GetPath(void) {
    return g_log_path;
}

void Logger_Shutdown(void) {
    if (!g_log_initialized) return;
    Logger_Write(L"app", L"로그를 종료합니다.");
    g_log_initialized = FALSE;
    DeleteCriticalSection(&g_log_lock);
    g_log_path[0] = 0;
}
