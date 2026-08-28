#include "history.h"
#include "filename.h"

#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define HISTORY_BUCKETS 1024
#define HISTORY_ID_CCH 128
#define HISTORY_FILENAME_CCH 256

typedef struct HistoryEntry {
    wchar_t id[HISTORY_ID_CCH];
    wchar_t filename[HISTORY_FILENAME_CCH];
    int bitrate;
    struct HistoryEntry *next;
} HistoryEntry;

static CRITICAL_SECTION g_history_lock;
static BOOL g_history_initialized = FALSE;
static wchar_t g_loaded_folder[MAX_PATH];
static HistoryEntry *g_entries[HISTORY_BUCKETS];

static unsigned HistoryHash(const wchar_t *id) {
    unsigned hash = 2166136261u;
    for (size_t i = 0; id && id[i]; ++i) {
        hash ^= (unsigned)towlower(id[i]);
        hash *= 16777619u;
    }
    return hash % HISTORY_BUCKETS;
}

static void ClearUnlocked(void) {
    for (size_t i = 0; i < HISTORY_BUCKETS; ++i) {
        HistoryEntry *entry = g_entries[i];
        while (entry) {
            HistoryEntry *next = entry->next;
            free(entry);
            entry = next;
        }
        g_entries[i] = NULL;
    }
}

static BOOL FileExists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static HistoryEntry *FindExactUnlocked(const wchar_t *id, int bitrate,
                                       const wchar_t *filename) {
    if (!id || !*id) return NULL;
    for (HistoryEntry *entry = g_entries[HistoryHash(id)]; entry; entry = entry->next) {
        if (!_wcsicmp(entry->id, id) && entry->bitrate == bitrate &&
            !_wcsicmp(entry->filename, filename ? filename : L"")) return entry;
    }
    return NULL;
}

static BOOL IsSupportedBitrate(int bitrate) {
    return bitrate == 128 || bitrate == 192 || bitrate == 256 || bitrate == 320;
}

static void InsertUnlocked(const wchar_t *id, int bitrate, const wchar_t *filename) {
    if (!id || !*id || FindExactUnlocked(id, bitrate, filename)) return;
    HistoryEntry *entry = (HistoryEntry *)calloc(1, sizeof(*entry));
    if (!entry) return;
    StringCchCopyW(entry->id, HISTORY_ID_CCH, id);
    StringCchCopyW(entry->filename, HISTORY_FILENAME_CCH, filename ? filename : L"");
    entry->bitrate = bitrate;
    unsigned bucket = HistoryHash(id);
    entry->next = g_entries[bucket];
    g_entries[bucket] = entry;
}

BOOL History_GetPath(const wchar_t *folder, wchar_t *out, size_t cch) {
    return folder && *folder && out && cch &&
           SUCCEEDED(StringCchPrintfW(out, cch, L"%s\\download_history.txt", folder));
}

BOOL History_Init(void) {
    if (g_history_initialized) return TRUE;
    InitializeCriticalSection(&g_history_lock);
    g_history_initialized = TRUE;
    return TRUE;
}

static BOOL ParseLineUnlocked(char *line) {
    if (!line || !*line || line[0] == '#') return FALSE;
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) {
        line[--length] = 0;
    }
    if (!*line) return FALSE;

    wchar_t wide[1024];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line, -1, wide,
                             (int)(sizeof(wide) / sizeof(wide[0])))) return FALSE;
    wchar_t *first_tab = wcschr(wide, L'\t');
    if (!first_tab) {
        /* Legacy history predates the quality selector and used 320 kbps. */
        InsertUnlocked(wide, 320, L"");
        return TRUE;
    }
    *first_tab = 0;
    wchar_t *second = first_tab + 1;
    wchar_t *second_tab = wcschr(second, L'\t');
    if (!second_tab) return FALSE;
    *second_tab = 0;
    int bitrate = _wtoi(second);
    const wchar_t *filename = second_tab + 1;
    if (!IsSupportedBitrate(bitrate) || !Filename_IsSafe(filename)) return FALSE;
    InsertUnlocked(wide, bitrate, filename);
    return FALSE;
}

static void RewriteV2Unlocked(const wchar_t *path) {
    wchar_t temporary[MAX_PATH];
    if (!path || FAILED(StringCchPrintfW(temporary, MAX_PATH, L"%s.tmp", path))) return;
    FILE *file = _wfopen(temporary, L"wb");
    if (!file) return;
    BOOL success = fputs("# Febius Downrush download history v2\r\n", file) >= 0;
    for (size_t i = 0; success && i < HISTORY_BUCKETS; ++i) {
        for (HistoryEntry *entry = g_entries[i]; entry; entry = entry->next) {
            char id_utf8[512], filename_utf8[1024];
            int id_bytes = WideCharToMultiByte(CP_UTF8, 0, entry->id, -1,
                                               id_utf8, (int)sizeof(id_utf8), NULL, NULL);
            int name_bytes = WideCharToMultiByte(CP_UTF8, 0, entry->filename, -1,
                                                 filename_utf8, (int)sizeof(filename_utf8), NULL, NULL);
            if (id_bytes <= 0 || name_bytes <= 0 ||
                fprintf(file, "%s\t%d\t%s\r\n", id_utf8, entry->bitrate, filename_utf8) <= 0) {
                success = FALSE;
                break;
            }
        }
    }
    if (fflush(file) != 0) success = FALSE;
    fclose(file);
    if (success) MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    else DeleteFileW(temporary);
}

void History_EnsureLoaded(const wchar_t *folder) {
    if (!g_history_initialized || !folder || !*folder) return;
    EnterCriticalSection(&g_history_lock);
    if (!_wcsicmp(g_loaded_folder, folder)) {
        LeaveCriticalSection(&g_history_lock);
        return;
    }
    ClearUnlocked();
    StringCchCopyW(g_loaded_folder, MAX_PATH, folder);

    wchar_t path[MAX_PATH];
    if (History_GetPath(folder, path, MAX_PATH)) {
        FILE *file = _wfopen(path, L"rb");
        if (file) {
            char line[2048];
            BOOL legacy_found = FALSE;
            while (fgets(line, sizeof(line), file)) {
                if (ParseLineUnlocked(line)) legacy_found = TRUE;
            }
            fclose(file);
            if (legacy_found) RewriteV2Unlocked(path);
        }
    }
    LeaveCriticalSection(&g_history_lock);
}

BOOL History_ShouldSkip(const wchar_t *folder,
                        const wchar_t *video_id,
                        const wchar_t *expected_filename,
                        int bitrate) {
    if (!folder || !video_id || !*video_id || !expected_filename || !*expected_filename) return FALSE;
    if (!IsSupportedBitrate(bitrate) || !Filename_IsSafe(expected_filename)) return FALSE;
    History_EnsureLoaded(folder);
    BOOL found = FALSE;
    EnterCriticalSection(&g_history_lock);
    for (HistoryEntry *entry = g_entries[HistoryHash(video_id)]; entry; entry = entry->next) {
        if (_wcsicmp(entry->id, video_id)) continue;
        if (entry->bitrate && entry->bitrate != bitrate) continue;
        const wchar_t *filename = entry->filename[0] ? entry->filename : expected_filename;
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(StringCchPrintfW(path, MAX_PATH, L"%s\\%s", folder, filename)) &&
            FileExists(path)) {
            found = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&g_history_lock);
    return found;
}

BOOL History_Record(const wchar_t *folder,
                    const wchar_t *video_id,
                    const wchar_t *actual_filename,
                    int bitrate) {
    if (!folder || !*folder || !video_id || !*video_id ||
        !actual_filename || !*actual_filename || !IsSupportedBitrate(bitrate) ||
        !Filename_IsSafe(video_id) || !Filename_IsSafe(actual_filename)) return FALSE;
    History_EnsureLoaded(folder);
    EnterCriticalSection(&g_history_lock);
    if (FindExactUnlocked(video_id, bitrate, actual_filename)) {
        LeaveCriticalSection(&g_history_lock);
        return TRUE;
    }

    wchar_t path[MAX_PATH];
    BOOL success = FALSE;
    if (History_GetPath(folder, path, MAX_PATH)) {
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        BOOL new_file = !GetFileAttributesExW(path, GetFileExInfoStandard, &attributes) ||
                        (!attributes.nFileSizeHigh && !attributes.nFileSizeLow);
        FILE *file = _wfopen(path, L"ab");
        if (file) {
            BOOL header_ok = !new_file ||
                fputs("# Febius Downrush download history v2\r\n", file) >= 0;
            char id_utf8[512], filename_utf8[1024];
            int id_bytes = WideCharToMultiByte(CP_UTF8, 0, video_id, -1,
                                               id_utf8, (int)sizeof(id_utf8), NULL, NULL);
            int name_bytes = WideCharToMultiByte(CP_UTF8, 0, actual_filename, -1,
                                                 filename_utf8, (int)sizeof(filename_utf8), NULL, NULL);
            if (header_ok && id_bytes > 0 && name_bytes > 0 &&
                fprintf(file, "%s\t%d\t%s\r\n", id_utf8, bitrate, filename_utf8) > 0 &&
                fflush(file) == 0) {
                success = TRUE;
            }
            if (fclose(file) != 0) success = FALSE;
        }
    }
    if (success) InsertUnlocked(video_id, bitrate, actual_filename);
    LeaveCriticalSection(&g_history_lock);
    return success;
}

void History_Invalidate(void) {
    if (!g_history_initialized) return;
    EnterCriticalSection(&g_history_lock);
    ClearUnlocked();
    g_loaded_folder[0] = 0;
    LeaveCriticalSection(&g_history_lock);
}

void History_Shutdown(void) {
    if (!g_history_initialized) return;
    History_Invalidate();
    g_history_initialized = FALSE;
    DeleteCriticalSection(&g_history_lock);
}
