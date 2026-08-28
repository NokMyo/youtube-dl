#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <process.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>

#include "command_line.h"
#include "filename.h"
#include "history.h"
#include "logger.h"
#include "process_runner.h"
#include "resource.h"
#include "version.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "advapi32.lib")

#define APP_TITLE L"Febius Downrush"
#define APP_SERIES L"Febius Utility Series"
#define APP_TAGLINE L"FAST · DIRECT · EFFICIENT"
#define MAX_JOBS 512
#define META_SEP L"<<<YTMP3>>>"
#define CLEAN_NAME_CCH 256
#define APP_CLIENT_W 960
#define APP_CLIENT_H 640
#define META_WORKER_COUNT 4
#define DOWNLOAD_WORKER_COUNT 2
#define IDT_SPLASH_ANIMATE 1
#define IDT_SHOW_MAIN 2
#define IDT_AUTO_UPDATE 3

#define IDC_URL_EDIT            1001
#define IDC_ADD_LINKS           1002
#define IDC_LIST                1005
#define IDC_FOLDER_EDIT         1020
#define IDC_FOLDER_BROWSE       1021
#define IDC_FOLDER_STATS        1023
#define IDC_DOWNLOAD_ALL        1030
#define IDC_PROGRESS            1040
#define IDC_STATUS              1041
#define IDC_OVERALL             1042
#define IDC_RAW_TITLE           1050
#define IDC_CLEAN_TITLE         1051
#define IDC_APPLY_FILENAME      1052
#define IDC_CANCEL_DOWNLOAD     1053

#define WM_APP_JOB_UPDATED      (WM_APP + 1)
#define WM_APP_REBUILD_LIST     (WM_APP + 2)
#define WM_APP_META_DONE        (WM_APP + 3)
#define WM_APP_OVERALL          (WM_APP + 6)
#define WM_APP_DOWNLOAD_DONE    (WM_APP + 7)
#define WM_APP_FOLDER_STATS     (WM_APP + 8)
#define WM_APP_TOOLS_READY      (WM_APP + 9)
#define WM_APP_UPDATE_RESULT    (WM_APP + 10)
#define WM_APP_SPLASH_STATUS    (WM_APP + 11)

#define IDM_FILE_LOAD_TXT       2001
#define IDM_FILE_BROWSE_FOLDER  2002
#define IDM_FILE_OPEN_FOLDER    2003
#define IDM_FILE_EXIT           2004
#define IDM_JOB_ADD_LINKS       2010
#define IDM_JOB_REMOVE_DUP      2011
#define IDM_JOB_DOWNLOAD_ALL    2012
#define IDM_JOB_RETRY_FAILED    2013
#define IDM_JOB_CLEAR           2014
#define IDM_JOB_DELETE_SELECTED 2015
#define IDM_TOOLS_REFRESH_STATS 2020
#define IDM_TOOLS_OPEN_HISTORY  2021
#define IDM_TOOLS_OPEN_LOG      2022
#define IDM_HELP_ABOUT          2030
#define IDM_HELP_CHECK_UPDATES  2031
#define IDM_HELP_RELEASES       2032
#define IDM_OPT_DEDUP           2040
#define IDM_OPT_SKIP            2041
#define IDM_OPT_SANITIZE        2042
#define IDM_OPT_CLEAN           2043
#define IDM_OPT_SIZE            2044
#define IDM_OPT_AUTO_UPDATE     2045
#define IDM_QUALITY_128         2050
#define IDM_QUALITY_192         2051
#define IDM_QUALITY_256         2052
#define IDM_QUALITY_320         2053

#define UPDATE_ERROR            0
#define UPDATE_CURRENT          1
#define UPDATE_AVAILABLE        2

typedef enum JobStatus {
    JOB_FETCHING = 0,
    JOB_READY,
    JOB_DOWNLOADING,
    JOB_DONE,
    JOB_FAILED,
    JOB_SKIPPED
} JobStatus;

typedef struct Job {
    wchar_t url[2048];
    wchar_t video_id[128];
    wchar_t raw_title[1024];
    wchar_t artist[512];
    wchar_t track[512];
    wchar_t clean_name[CLEAN_NAME_CCH];
    wchar_t error[768];
    ULONGLONG duration_ms;
    ULONGLONG expected_size;
    int progress;
    JobStatus status;
    BOOL custom_name;
} Job;

typedef struct MetaBatch {
    int start;
    int end;
} MetaBatch;

typedef struct DownloadBatch {
    BOOL failed_only;
    BOOL skip_existing;
    int bitrate;
    wchar_t folder[MAX_PATH];
    wchar_t temp_dir[MAX_PATH];
} DownloadBatch;

typedef struct MetaWork {
    volatile LONG next;
    int end;
} MetaWork;

typedef struct DownloadWork {
    int indices[MAX_JOBS];
    int total;
    volatile LONG next;
    volatile LONG done;
    BOOL skip_existing;
    int bitrate;
    wchar_t folder[MAX_PATH];
    wchar_t temp_dir[MAX_PATH];
} DownloadWork;

typedef struct FolderStatsResult {
    LONG generation;
    wchar_t folder[MAX_PATH];
    int count;
    ULONGLONG bytes;
} FolderStatsResult;

typedef struct UpdateResult {
    int state;
    BOOL automatic;
    wchar_t latest[32];
} UpdateResult;

typedef struct DownloadJobSnapshot {
    wchar_t url[2048];
    wchar_t video_id[128];
    wchar_t clean_name[CLEAN_NAME_CCH];
} DownloadJobSnapshot;

typedef struct MetadataLines {
    wchar_t meta[4096];
    wchar_t last[1024];
} MetadataLines;

typedef struct DownloadLines {
    int index;
    int last_progress;
    wchar_t last[1024];
} DownloadLines;

static HINSTANCE g_instance;
static HWND g_main;
static HWND g_splash;
static HWND g_url_edit;
static HWND g_list;
static HWND g_folder_edit;
static HWND g_folder_stats;
static HWND g_progress;
static HWND g_status;
static HWND g_overall;
static HWND g_raw_title;
static HWND g_clean_title;
static HWND g_apply_filename;
static HWND g_download_button;
static HWND g_cancel_button;
static HMENU g_options_menu;
static HMENU g_quality_menu;
static HFONT g_font;
static HFONT g_splash_title_font;
static HFONT g_splash_body_font;
static HFONT g_splash_brand_font;
static HFONT g_splash_small_font;
static HICON g_brand_symbol;
static HICON g_downrush_artwork;

static Job g_jobs[MAX_JOBS];
static int g_job_count = 0;
static CRITICAL_SECTION g_jobs_lock;
static CRITICAL_SECTION g_file_lock;
static CRITICAL_SECTION g_tools_lock;
static volatile LONG g_meta_running = 0;
static volatile LONG g_download_running = 0;
static volatile LONG g_tools_loading = 0;
static volatile LONG g_update_running = 0;
static volatile LONG g_startup_phase = 0;
static volatile LONG g_splash_progress = 8;
static volatile LONG g_stats_generation = 0;
static volatile LONG g_close_after_cancel = 0;
static volatile LONG g_opt_dedup = 1;
static volatile LONG g_opt_skip = 1;
static volatile LONG g_opt_sanitize = 1;
static volatile LONG g_opt_clean = 1;
static volatile LONG g_opt_size = 1;
static volatile LONG g_audio_bitrate = 320;
static volatile LONG g_auto_update = 1;

static wchar_t g_app_dir[MAX_PATH];
static wchar_t g_ytdlp[MAX_PATH];
static wchar_t g_ffmpeg[MAX_PATH];
static wchar_t g_ffmpeg_dir[MAX_PATH];
static wchar_t g_deno[MAX_PATH];
static wchar_t g_tools_dir[MAX_PATH];
static wchar_t g_local_app_dir[MAX_PATH];
static wchar_t g_saved_folder[MAX_PATH];
static HANDLE g_cancel_event;
static HANDLE g_instance_mutex;
static int g_main_show_command = SW_SHOWNORMAL;
static ULONGLONG g_splash_started = 0;
static UINT g_ui_dpi = 96;
static DWORD g_processor_count = 1;

static const wchar_t *RELEASES_URL = L"https://github.com/NokMyo/youtube-dl/releases";
static const wchar_t *SETTINGS_KEY = L"Software\\NokMyo\\Febius\\Downrush";
static const wchar_t *LEGACY_SETTINGS_KEYS[] = {
    L"Software\\NokMyo\\FebiusYTMP3Downloader",
    L"Software\\NokMyo\\SeowolYTMP3Downloader"
};

static int ScaleUi(int value) {
    return MulDiv(value, (int)g_ui_dpi, 96);
}

static int MetadataWorkerLimitFor(DWORD processor_count) {
    if (processor_count <= 2) return 1;
    if (processor_count <= 4) return 2;
    return META_WORKER_COUNT;
}

static int DownloadWorkerLimitFor(DWORD processor_count) {
    return processor_count <= 2 ? 1 : DOWNLOAD_WORKER_COUNT;
}

static int MetadataWorkerLimit(void) {
    return MetadataWorkerLimitFor(g_processor_count);
}

static int DownloadWorkerLimit(void) {
    return DownloadWorkerLimitFor(g_processor_count);
}

static BOOL IsSupportedBitrate(DWORD value) {
    return value == 128 || value == 192 || value == 256 || value == 320;
}

static BOOL ReadSettingValueAt(const wchar_t *key_path, const wchar_t *name,
                               DWORD expected_type, void *value, DWORD *value_size) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key_path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return FALSE;
    }
    DWORD type = 0;
    LONG status = RegQueryValueExW(key, name, NULL, &type, (BYTE *)value, value_size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == expected_type;
}

static BOOL ReadSettingValue(const wchar_t *name, DWORD expected_type,
                             void *value, DWORD *value_size) {
    return ReadSettingValueAt(SETTINGS_KEY, name, expected_type, value, value_size);
}

static void MigrateLegacySettings(void) {
    HKEY current = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_QUERY_VALUE, &current) == ERROR_SUCCESS) {
        RegCloseKey(current);
        return;
    }

    for (size_t i = 0; i < sizeof(LEGACY_SETTINGS_KEYS) / sizeof(LEGACY_SETTINGS_KEYS[0]); ++i) {
        HKEY legacy = NULL, destination = NULL;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, LEGACY_SETTINGS_KEYS[i], 0,
                          KEY_READ, &legacy) != ERROR_SUCCESS) continue;
        LONG copied = ERROR_GEN_FAILURE;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0,
                            KEY_WRITE, NULL, &destination, NULL) == ERROR_SUCCESS) {
            copied = RegCopyTreeW(legacy, NULL, destination);
            RegCloseKey(destination);
        }
        RegCloseKey(legacy);
        if (copied == ERROR_SUCCESS) {
            Logger_Write(L"migration", L"기존 설정을 Febius\\Downrush 설정으로 이전했습니다.");
            return;
        }
        RegDeleteTreeW(HKEY_CURRENT_USER, SETTINGS_KEY);
    }
}

static void WriteSettingValue(const wchar_t *name, DWORD type,
                              const void *value, DWORD value_size) {
    HKEY key = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    RegSetValueExW(key, name, 0, type, (const BYTE *)value, value_size);
    RegCloseKey(key);
}

static BOOL ReadSettingDword(const wchar_t *name, DWORD *value) {
    DWORD size = sizeof(*value);
    return ReadSettingValue(name, REG_DWORD, value, &size) && size == sizeof(*value);
}

static void WriteSettingDword(const wchar_t *name, DWORD value) {
    WriteSettingValue(name, REG_DWORD, &value, sizeof(value));
}

static BOOL ReadSettingQword(const wchar_t *name, ULONGLONG *value) {
    DWORD size = sizeof(*value);
    return ReadSettingValue(name, REG_QWORD, value, &size) && size == sizeof(*value);
}

static void WriteSettingQword(const wchar_t *name, ULONGLONG value) {
    WriteSettingValue(name, REG_QWORD, &value, sizeof(value));
}

static void WriteSettingString(const wchar_t *name, const wchar_t *value) {
    if (!value) value = L"";
    WriteSettingValue(name, REG_SZ, value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
}

static void LoadSettings(void) {
    MigrateLegacySettings();
    struct BooleanSetting {
        const wchar_t *name;
        volatile LONG *target;
    } boolean_settings[] = {
        { L"RemoveDuplicates", &g_opt_dedup },
        { L"SkipDownloaded", &g_opt_skip },
        { L"SanitizeFilenames", &g_opt_sanitize },
        { L"CleanNames", &g_opt_clean },
        { L"ShowEstimatedSize", &g_opt_size },
        { L"CheckUpdatesAtStartup", &g_auto_update }
    };
    for (size_t i = 0; i < sizeof(boolean_settings) / sizeof(boolean_settings[0]); ++i) {
        DWORD value = 0;
        if (ReadSettingDword(boolean_settings[i].name, &value)) {
            InterlockedExchange((LONG *)boolean_settings[i].target, value ? 1 : 0);
        }
    }

    DWORD bitrate = 0;
    if (ReadSettingDword(L"AudioBitrate", &bitrate) && IsSupportedBitrate(bitrate)) {
        InterlockedExchange((LONG *)&g_audio_bitrate, (LONG)bitrate);
    }

    DWORD folder_size = sizeof(g_saved_folder);
    if (!ReadSettingValue(L"OutputFolder", REG_SZ, g_saved_folder, &folder_size)) {
        g_saved_folder[0] = 0;
    } else {
        g_saved_folder[MAX_PATH - 1] = 0;
    }
}

static ULONGLONG CurrentFileTimeValue(void) {
    FILETIME file_time;
    ULARGE_INTEGER value;
    GetSystemTimeAsFileTime(&file_time);
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}

static BOOL ShouldCheckUpdatesAutomatically(void) {
    if (!InterlockedCompareExchange((LONG *)&g_auto_update, 0, 0)) return FALSE;
    ULONGLONG last = 0;
    ULONGLONG now = CurrentFileTimeValue();
    const ULONGLONG one_day = 24ULL * 60ULL * 60ULL * 10000000ULL;
    if (ReadSettingQword(L"LastSuccessfulUpdateCheck", &last) &&
        now >= last && now - last < one_day) return FALSE;
    return TRUE;
}

static BOOL FileExistsW2(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL DirectoryExistsW2(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL PathsEqualIgnoringTrailingSeparators(const wchar_t *left,
                                                  const wchar_t *right) {
    if (!left || !right) return FALSE;
    size_t left_length = wcslen(left);
    size_t right_length = wcslen(right);
    while (left_length > 3 &&
           (left[left_length - 1] == L'\\' || left[left_length - 1] == L'/')) {
        --left_length;
    }
    while (right_length > 3 &&
           (right[right_length - 1] == L'\\' || right[right_length - 1] == L'/')) {
        --right_length;
    }
    return left_length == right_length &&
           !_wcsnicmp(left, right, left_length);
}

static void GetAppDirectory(wchar_t *out, size_t cch) {
    DWORD n = GetModuleFileNameW(NULL, out, (DWORD)cch);
    if (!n || n >= cch) {
        StringCchCopyW(out, cch, L".");
        return;
    }
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash) *slash = L'\0';
}

static void MoveLegacyFileIfNeeded(const wchar_t *source, const wchar_t *destination) {
    if (!source || !destination || FileExistsW2(destination) || !FileExistsW2(source)) return;
    MoveFileExW(source, destination, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
}

static BOOL InitializeAppDataPaths(void) {
    wchar_t local[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                NULL, SHGFP_TYPE_CURRENT, local))) return FALSE;
    wchar_t brand_dir[MAX_PATH];
    if (FAILED(StringCchPrintfW(brand_dir, MAX_PATH, L"%s\\Febius", local)) ||
        FAILED(StringCchPrintfW(g_local_app_dir, MAX_PATH,
                                L"%s\\Febius\\Downrush", local))) return FALSE;
    if (SHCreateDirectoryExW(NULL, brand_dir, NULL) != ERROR_SUCCESS &&
        !DirectoryExistsW2(brand_dir)) return FALSE;

    const wchar_t *legacy_names[] = { L"FebiusYTMP3Downloader", L"SeowolYTMP3Downloader" };
    wchar_t legacy_paths[2][MAX_PATH];
    ZeroMemory(legacy_paths, sizeof(legacy_paths));
    for (size_t i = 0; i < sizeof(legacy_names) / sizeof(legacy_names[0]); ++i) {
        if (FAILED(StringCchPrintfW(legacy_paths[i], MAX_PATH,
                                    L"%s\\%s", local, legacy_names[i]))) continue;
        if (!DirectoryExistsW2(g_local_app_dir) && DirectoryExistsW2(legacy_paths[i])) {
            MoveFileExW(legacy_paths[i], g_local_app_dir,
                        MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
        }
    }
    if (SHCreateDirectoryExW(NULL, g_local_app_dir, NULL) != ERROR_SUCCESS &&
        !DirectoryExistsW2(g_local_app_dir)) return FALSE;

    wchar_t current_tools[MAX_PATH], current_logs[MAX_PATH], downrush_log[MAX_PATH];
    wchar_t downrush_old_log[MAX_PATH];
    if (FAILED(StringCchPrintfW(current_tools, MAX_PATH, L"%s\\tools", g_local_app_dir)) ||
        FAILED(StringCchPrintfW(current_logs, MAX_PATH, L"%s\\logs", g_local_app_dir)) ||
        FAILED(StringCchPrintfW(downrush_log, MAX_PATH, L"%s\\Downrush.log", current_logs)) ||
        FAILED(StringCchPrintfW(downrush_old_log, MAX_PATH,
                                L"%s\\Downrush.log.old", current_logs))) {
        return FALSE;
    }
    SHCreateDirectoryExW(NULL, current_logs, NULL);

    wchar_t old_log[MAX_PATH], old_rotated_log[MAX_PATH];
    if (SUCCEEDED(StringCchPrintfW(old_log, MAX_PATH, L"%s\\Febius.log", current_logs))) {
        MoveLegacyFileIfNeeded(old_log, downrush_log);
    }
    if (SUCCEEDED(StringCchPrintfW(old_rotated_log, MAX_PATH,
                                   L"%s\\Febius.log.old", current_logs))) {
        MoveLegacyFileIfNeeded(old_rotated_log, downrush_old_log);
    }
    for (size_t i = 0; i < sizeof(legacy_paths) / sizeof(legacy_paths[0]); ++i) {
        if (!legacy_paths[i][0] || !DirectoryExistsW2(legacy_paths[i])) continue;
        wchar_t legacy_tools[MAX_PATH], legacy_log[MAX_PATH], legacy_rotated_log[MAX_PATH];
        if (!DirectoryExistsW2(current_tools) &&
            SUCCEEDED(StringCchPrintfW(legacy_tools, MAX_PATH, L"%s\\tools", legacy_paths[i])) &&
            DirectoryExistsW2(legacy_tools)) {
            MoveFileExW(legacy_tools, current_tools,
                        MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
        }
        if (SUCCEEDED(StringCchPrintfW(legacy_log, MAX_PATH,
                                       L"%s\\logs\\Febius.log", legacy_paths[i]))) {
            MoveLegacyFileIfNeeded(legacy_log, downrush_log);
        }
        if (SUCCEEDED(StringCchPrintfW(legacy_rotated_log, MAX_PATH,
                                       L"%s\\logs\\Febius.log.old", legacy_paths[i]))) {
            MoveLegacyFileIfNeeded(legacy_rotated_log, downrush_old_log);
        }
    }
    return TRUE;
}

static BOOL GetDefaultOutputFolder(wchar_t *out, size_t cch) {
    wchar_t music[MAX_PATH];
    if (!out || !cch || FAILED(SHGetFolderPathW(NULL,
            CSIDL_MYMUSIC | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, music))) return FALSE;
    return SUCCEEDED(StringCchPrintfW(out, cch, L"%s\\Febius\\Downrush", music));
}

static BOOL GetLegacyDefaultOutputFolder(wchar_t *out, size_t cch) {
    wchar_t music[MAX_PATH];
    if (!out || !cch || FAILED(SHGetFolderPathW(NULL,
            CSIDL_MYMUSIC | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, music))) return FALSE;
    return SUCCEEDED(StringCchPrintfW(out, cch, L"%s\\YouTubeMP3", music));
}

static void MigrateLegacyDefaultOutputFolder(void) {
    wchar_t legacy[MAX_PATH], current[MAX_PATH];
    if (!GetLegacyDefaultOutputFolder(legacy, MAX_PATH) ||
        !GetDefaultOutputFolder(current, MAX_PATH)) return;
    if (g_saved_folder[0] &&
        !PathsEqualIgnoringTrailingSeparators(g_saved_folder, legacy)) return;

    BOOL legacy_exists = DirectoryExistsW2(legacy);
    BOOL current_exists = DirectoryExistsW2(current);
    if (legacy_exists && !current_exists) {
        wchar_t brand_folder[MAX_PATH];
        StringCchCopyW(brand_folder, MAX_PATH, current);
        wchar_t *slash = wcsrchr(brand_folder, L'\\');
        if (slash) *slash = 0;
        if (slash && (SHCreateDirectoryExW(NULL, brand_folder, NULL) == ERROR_SUCCESS ||
                      DirectoryExistsW2(brand_folder))) {
            MoveFileExW(legacy, current, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
            current_exists = DirectoryExistsW2(current);
            if (current_exists) {
                legacy_exists = DirectoryExistsW2(legacy);
                Logger_Write(L"migration", L"기본 음악 폴더를 Music\\Febius\\Downrush로 이전했습니다.");
            }
        }
    }

    if (!legacy_exists && !current_exists) {
        SHCreateDirectoryExW(NULL, current, NULL);
        current_exists = DirectoryExistsW2(current);
    }

    if (current_exists) {
        if (legacy_exists) {
            Logger_Write(L"migration",
                L"새 기본 음악 폴더가 이미 있어 기존 Music\\YouTubeMP3 폴더는 보존했습니다.");
        }
        StringCchCopyW(g_saved_folder, MAX_PATH, current);
        WriteSettingString(L"OutputFolder", current);
    } else if (legacy_exists) {
        StringCchCopyW(g_saved_folder, MAX_PATH, legacy);
        WriteSettingString(L"OutputFolder", legacy);
        Logger_Write(L"migration", L"기존 음악 폴더 이전에 실패해 원래 경로를 유지합니다.");
    }
}

static void SetStartupPhase(LONG phase) {
    InterlockedExchange((LONG *)&g_startup_phase, phase);
    HWND splash = g_splash;
    if (splash) PostMessageW(splash, WM_APP_SPLASH_STATUS, 0, 0);
}

static BOOL FindTool(const wchar_t *name, wchar_t *out, size_t cch) {
    wchar_t local[MAX_PATH];
    if (g_tools_dir[0]) {
        StringCchPrintfW(local, MAX_PATH, L"%s\\%s", g_tools_dir, name);
        if (FileExistsW2(local)) {
            StringCchCopyW(out, cch, local);
            return TRUE;
        }
    }
    StringCchPrintfW(local, MAX_PATH, L"%s\\%s", g_app_dir, name);
    if (FileExistsW2(local)) {
        StringCchCopyW(out, cch, local);
        return TRUE;
    }
    out[0] = 0;
    return FALSE;
}

static void DirectoryFromPath(const wchar_t *path, wchar_t *out, size_t cch) {
    StringCchCopyW(out, cch, path);
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash) *slash = L'\0';
}

static BOOL RunHiddenProcess(const wchar_t *command, DWORD timeout_ms) {
    ProcessResult result;
    BOOL ran = Process_RunHidden(command, g_cancel_event, timeout_ms, &result);
    if (!ran || result.exit_code != 0) {
        if (result.timed_out) Logger_Write(L"process", L"숨김 프로세스가 시간 제한을 초과했습니다.");
        else if (result.cancelled) Logger_Write(L"process", L"숨김 프로세스가 취소되었습니다.");
        else Logger_Write(L"process", L"숨김 프로세스 실행에 실패했습니다.");
        return FALSE;
    }
    return TRUE;
}

static BOOL BundledToolFilesExist(void) {
    if (!g_tools_dir[0]) return FALSE;
    wchar_t p1[MAX_PATH], p2[MAX_PATH], p3[MAX_PATH], p4[MAX_PATH];
    StringCchPrintfW(p1, MAX_PATH, L"%s\\yt-dlp.exe", g_tools_dir);
    StringCchPrintfW(p2, MAX_PATH, L"%s\\ffmpeg.exe", g_tools_dir);
    StringCchPrintfW(p3, MAX_PATH, L"%s\\ffprobe.exe", g_tools_dir);
    StringCchPrintfW(p4, MAX_PATH, L"%s\\deno.exe", g_tools_dir);
    return FileExistsW2(p1) && FileExistsW2(p2) && FileExistsW2(p3) && FileExistsW2(p4);
}

static BOOL ReadBundleStamp(ULONGLONG expected) {
    wchar_t path[MAX_PATH];
    StringCchPrintfW(path, MAX_PATH, L"%s\\payload.stamp", g_tools_dir);
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    wchar_t text[64];
    DWORD got = 0;
    BOOL ok = ReadFile(h, text, sizeof(text) - sizeof(wchar_t), &got, NULL);
    CloseHandle(h);
    if (!ok) return FALSE;
    text[got / sizeof(wchar_t)] = 0;
    return _wcstoui64(text, NULL, 10) == expected;
}

static void WriteBundleStamp(ULONGLONG value) {
    wchar_t path[MAX_PATH], text[64];
    StringCchPrintfW(path, MAX_PATH, L"%s\\payload.stamp", g_tools_dir);
    StringCchPrintfW(text, 64, L"%llu", value);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(h, text, (DWORD)(wcslen(text) * sizeof(wchar_t)), &wrote, NULL);
    CloseHandle(h);
}

static ULONGLONG BundleStampFromAttributes(const WIN32_FILE_ATTRIBUTE_DATA *attributes) {
    ULARGE_INTEGER size, modified;
    size.HighPart = attributes->nFileSizeHigh;
    size.LowPart = attributes->nFileSizeLow;
    modified.HighPart = attributes->ftLastWriteTime.dwHighDateTime;
    modified.LowPart = attributes->ftLastWriteTime.dwLowDateTime;
    return size.QuadPart ^ modified.QuadPart ^ 0x53594D5033504B31ULL;
}

static BOOL GetPayloadEndOffset(HANDLE executable, LONGLONG file_size, LONGLONG *payload_end) {
    if (!payload_end || executable == INVALID_HANDLE_VALUE || file_size <= 0) return FALSE;
    *payload_end = file_size;

    IMAGE_DOS_HEADER dos_header;
    LARGE_INTEGER position;
    position.QuadPart = 0;
    DWORD received = 0;
    if (!SetFilePointerEx(executable, position, NULL, FILE_BEGIN) ||
        !ReadFile(executable, &dos_header, sizeof(dos_header), &received, NULL) ||
        received != sizeof(dos_header) || dos_header.e_magic != IMAGE_DOS_SIGNATURE ||
        dos_header.e_lfanew <= 0) return TRUE;

    IMAGE_NT_HEADERS64 nt_headers;
    position.QuadPart = dos_header.e_lfanew;
    received = 0;
    if (!SetFilePointerEx(executable, position, NULL, FILE_BEGIN) ||
        !ReadFile(executable, &nt_headers, sizeof(nt_headers), &received, NULL) ||
        received != sizeof(nt_headers) || nt_headers.Signature != IMAGE_NT_SIGNATURE ||
        nt_headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return TRUE;

    IMAGE_DATA_DIRECTORY security =
        nt_headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    ULONGLONG certificate_end = (ULONGLONG)security.VirtualAddress + security.Size;
    if (security.VirtualAddress && security.Size &&
        certificate_end == (ULONGLONG)file_size && security.VirtualAddress >= 16) {
        *payload_end = security.VirtualAddress;
    }
    return TRUE;
}

static BOOL FindBundleFooterEnd(HANDLE executable, LONGLONG search_end, LONGLONG *footer_end) {
    if (!footer_end || search_end < 16) return FALSE;
    for (LONGLONG padding = 0; padding <= 7 && search_end - padding >= 16; ++padding) {
        LARGE_INTEGER position;
        position.QuadPart = search_end - padding - 8;
        unsigned char magic[8];
        DWORD received = 0;
        if (SetFilePointerEx(executable, position, NULL, FILE_BEGIN) &&
            ReadFile(executable, magic, sizeof(magic), &received, NULL) &&
            received == sizeof(magic) && !memcmp(magic, "YTMP3PK1", sizeof(magic))) {
            *footer_end = search_end - padding;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL PrepareBundledTools(void) {
    if (!g_local_app_dir[0] && !InitializeAppDataPaths()) return FALSE;
    EnterCriticalSection(&g_tools_lock);
    StringCchPrintfW(g_tools_dir, MAX_PATH, L"%s\\tools", g_local_app_dir);
    LeaveCriticalSection(&g_tools_lock);
    SHCreateDirectoryExW(NULL, g_tools_dir, NULL);

    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return FALSE;
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExW(exe, GetFileExInfoStandard, &attributes)) return FALSE;
    LARGE_INTEGER total;
    total.HighPart = (LONG)attributes.nFileSizeHigh;
    total.LowPart = attributes.nFileSizeLow;
    if (total.QuadPart < 16) return FALSE;
    ULONGLONG package_stamp = BundleStampFromAttributes(&attributes);

    /* The normal launch path stops here without opening the ~95 MB package. */
    if (BundledToolFilesExist() && ReadBundleStamp(package_stamp)) return TRUE;

    HANDLE in = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (in == INVALID_HANDLE_VALUE) return FALSE;

    LONGLONG payload_search_end = total.QuadPart, payload_end = 0;
    if (!GetPayloadEndOffset(in, total.QuadPart, &payload_search_end) ||
        !FindBundleFooterEnd(in, payload_search_end, &payload_end)) {
        CloseHandle(in);
        return FALSE;
    }

    LARGE_INTEGER footer_pos;
    footer_pos.QuadPart = payload_end - 16;
    if (!SetFilePointerEx(in, footer_pos, NULL, FILE_BEGIN)) {
        CloseHandle(in);
        return FALSE;
    }

    unsigned char footer[16];
    DWORD got = 0;
    if (!ReadFile(in, footer, sizeof(footer), &got, NULL) || got != sizeof(footer) ||
        memcmp(footer + 8, "YTMP3PK1", 8) != 0) {
        CloseHandle(in);
        return FALSE;
    }

    ULONGLONG payload_size = 0;
    memcpy(&payload_size, footer, sizeof(payload_size));
    if (!payload_size || payload_size > (ULONGLONG)payload_end - 16ULL) {
        CloseHandle(in);
        return FALSE;
    }

    SetStartupPhase(2);

    wchar_t temp_dir[MAX_PATH], zip_path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp_dir)) {
        CloseHandle(in);
        return FALSE;
    }
    if (!GetTempFileNameW(temp_dir, L"SYT", 0, zip_path)) {
        CloseHandle(in);
        return FALSE;
    }
    HANDLE out = CreateFileW(zip_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        CloseHandle(in);
        DeleteFileW(zip_path);
        return FALSE;
    }

    LARGE_INTEGER start;
    start.QuadPart = payload_end - 16 - (LONGLONG)payload_size;
    if (!SetFilePointerEx(in, start, NULL, FILE_BEGIN)) {
        CloseHandle(out);
        CloseHandle(in);
        DeleteFileW(zip_path);
        return FALSE;
    }

    const DWORD copy_buffer_size = 1024 * 1024;
    unsigned char *buffer = (unsigned char *)malloc(copy_buffer_size);
    if (!buffer) {
        CloseHandle(out);
        CloseHandle(in);
        DeleteFileW(zip_path);
        return FALSE;
    }

    ULONGLONG remaining = payload_size;
    BOOL copy_ok = TRUE;
    while (remaining) {
        DWORD want = remaining > copy_buffer_size ? copy_buffer_size : (DWORD)remaining;
        DWORD read_bytes = 0, wrote = 0;
        if (!ReadFile(in, buffer, want, &read_bytes, NULL) || read_bytes != want ||
            !WriteFile(out, buffer, read_bytes, &wrote, NULL) || wrote != read_bytes) {
            copy_ok = FALSE;
            break;
        }
        remaining -= read_bytes;
    }
    free(buffer);
    CloseHandle(out);
    CloseHandle(in);
    if (!copy_ok) {
        DeleteFileW(zip_path);
        return FALSE;
    }

    wchar_t command[4096], system_dir[MAX_PATH] = L"", tar_path[MAX_PATH] = L"";
    DWORD system_length = GetSystemDirectoryW(system_dir, MAX_PATH);
    BOOL have_system_dir = system_length > 0 && system_length < MAX_PATH;
    if (have_system_dir) StringCchPrintfW(tar_path, MAX_PATH, L"%s\\tar.exe", system_dir);
    const wchar_t *const tar_args[] = { tar_path, L"-xf", zip_path, L"-C", g_tools_dir };
    BOOL extracted = have_system_dir && FileExistsW2(tar_path) &&
                     CommandLine_Build(command, 4096, tar_args, sizeof(tar_args) / sizeof(tar_args[0])) &&
                     RunHiddenProcess(command, 120000);
    if (!extracted) {
        wchar_t escaped_zip[MAX_PATH * 2];
        wchar_t escaped_tools[MAX_PATH * 2];
        wchar_t script[2048];
        if (PowerShell_EscapeSingleQuoted(zip_path, escaped_zip, MAX_PATH * 2) &&
            PowerShell_EscapeSingleQuoted(g_tools_dir, escaped_tools, MAX_PATH * 2) &&
            SUCCEEDED(StringCchPrintfW(script, 2048,
                L"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force",
                escaped_zip, escaped_tools))) {
            wchar_t powershell_path[MAX_PATH];
            if (FAILED(StringCchPrintfW(powershell_path, MAX_PATH,
                    L"%s\\WindowsPowerShell\\v1.0\\powershell.exe", system_dir))) {
                powershell_path[0] = 0;
            }
            const wchar_t *const powershell_args[] = {
                powershell_path, L"-NoProfile", L"-NonInteractive",
                L"-ExecutionPolicy", L"Bypass", L"-Command", script
            };
            extracted = have_system_dir && FileExistsW2(powershell_path) &&
                        CommandLine_Build(command, 4096, powershell_args,
                                          sizeof(powershell_args) / sizeof(powershell_args[0])) &&
                        RunHiddenProcess(command, 120000);
        }
    }
    DeleteFileW(zip_path);

    if (extracted && BundledToolFilesExist()) {
        WriteBundleStamp(package_stamp);
        return TRUE;
    }
    return FALSE;
}

static void RefreshTools(void) {
    EnterCriticalSection(&g_tools_lock);
    g_ytdlp[0] = 0;
    g_ffmpeg[0] = 0;
    g_ffmpeg_dir[0] = 0;
    g_deno[0] = 0;
    FindTool(L"yt-dlp.exe", g_ytdlp, MAX_PATH);
    if (FindTool(L"ffmpeg.exe", g_ffmpeg, MAX_PATH)) {
        DirectoryFromPath(g_ffmpeg, g_ffmpeg_dir, MAX_PATH);
    }
    FindTool(L"deno.exe", g_deno, MAX_PATH);
    LeaveCriticalSection(&g_tools_lock);
}

static BOOL ToolsAvailable(void) {
    EnterCriticalSection(&g_tools_lock);
    BOOL available = g_ytdlp[0] && g_ffmpeg[0] && g_deno[0] &&
                     FileExistsW2(g_ytdlp) && FileExistsW2(g_ffmpeg) && FileExistsW2(g_deno);
    LeaveCriticalSection(&g_tools_lock);
    return available;
}

static BOOL ToolExecutableWorks(const wchar_t *path, const wchar_t *argument) {
    if (!path || !*path || !FileExistsW2(path)) return FALSE;
    wchar_t command[MAX_PATH * 2];
    const wchar_t *const arguments[] = { path, argument };
    if (!CommandLine_Build(command, MAX_PATH * 2, arguments,
                           sizeof(arguments) / sizeof(arguments[0]))) return FALSE;
    ProcessResult result;
    return Process_RunHidden(command, g_cancel_event, 30000, &result) &&
           !result.cancelled && !result.timed_out && result.exit_code == 0;
}

static BOOL HasCommandLineSwitch(const wchar_t *wanted) {
    int count = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return FALSE;
    BOOL found = FALSE;
    for (int i = 1; i < count; ++i) {
        if (!_wcsicmp(arguments[i], wanted)) {
            found = TRUE;
            break;
        }
    }
    LocalFree(arguments);
    return found;
}

static void TrimInPlace(wchar_t *s) {
    if (!s || !*s) return;
    wchar_t *start = s;
    while (*start && iswspace(*start)) start++;
    if (start != s) memmove(s, start, (wcslen(start) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n && iswspace(s[n - 1])) s[--n] = 0;
}

static BOOL IsNA(const wchar_t *s) {
    return !s || !*s || !_wcsicmp(s, L"NA") || !_wcsicmp(s, L"None") || !_wcsicmp(s, L"null");
}

static void BuildCleanFilename(const Job *job, wchar_t *out, size_t cch) {
    BOOL clean = InterlockedCompareExchange((LONG *)&g_opt_clean, 0, 0) ? TRUE : FALSE;
    BOOL sanitize = InterlockedCompareExchange((LONG *)&g_opt_sanitize, 0, 0) ? TRUE : FALSE;
    Filename_BuildClean(job->raw_title, job->artist, job->track, clean, sanitize, out, cch);
}

static const wchar_t *StatusText(JobStatus status) {
    switch (status) {
        case JOB_FETCHING: return L"정보 조회 중";
        case JOB_READY: return L"대기";
        case JOB_DOWNLOADING: return L"다운로드";
        case JOB_DONE: return L"완료";
        case JOB_FAILED: return L"실패";
        case JOB_SKIPPED: return L"이미 받음";
        default: return L"-";
    }
}

static ULONGLONG EstimatedMp3Bytes(ULONGLONG duration_ms, int bitrate) {
    if (!duration_ms || !IsSupportedBitrate((DWORD)bitrate)) return 0;
    return duration_ms * (ULONGLONG)bitrate / 8ULL;
}

static void FormatBytes(ULONGLONG bytes, wchar_t *out, size_t cch) {
    if (bytes == 0) {
        StringCchCopyW(out, cch, L"-");
    } else if (bytes < 1024ULL) {
        StringCchPrintfW(out, cch, L"%llu B", bytes);
    } else if (bytes < 1024ULL * 1024ULL) {
        StringCchPrintfW(out, cch, L"%.1f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        StringCchPrintfW(out, cch, L"%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        StringCchPrintfW(out, cch, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

static void ApplyClassic(HWND h) {
    if (!h) return;
    SetWindowTheme(h, L"", L"");
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
}

static void SetControlText(HWND h, const wchar_t *text) {
    if (!h) return;
    if (h == g_status) SendMessageW(h, SB_SETTEXTW, 0, (LPARAM)(text ? text : L""));
    else SetWindowTextW(h, text ? text : L"");
}

static void UpdateListRow(int index) {
    wchar_t raw_title[1024], clean_name[CLEAN_NAME_CCH];
    ULONGLONG expected_size;
    int progress;
    JobStatus status;
    EnterCriticalSection(&g_jobs_lock);
    if (index < 0 || index >= g_job_count) {
        LeaveCriticalSection(&g_jobs_lock);
        return;
    }
    StringCchCopyW(raw_title, 1024, g_jobs[index].raw_title);
    StringCchCopyW(clean_name, CLEAN_NAME_CCH, g_jobs[index].clean_name);
    expected_size = g_jobs[index].expected_size;
    progress = g_jobs[index].progress;
    status = g_jobs[index].status;
    LeaveCriticalSection(&g_jobs_lock);

    wchar_t no[32], size[64], state[64];
    StringCchPrintfW(no, 32, L"%d", index + 1);
    if (InterlockedCompareExchange((LONG *)&g_opt_size, 0, 0)) FormatBytes(expected_size, size, 64);
    else StringCchCopyW(size, 64, L"-");
    if (status == JOB_DOWNLOADING) {
        if (progress >= 100) StringCchCopyW(state, 64, L"MP3 변환 중");
        else StringCchPrintfW(state, 64, L"다운로드 %d%%", progress);
    } else {
        StringCchCopyW(state, 64, StatusText(status));
    }

    LVITEMW item = {0};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = no;
    item.lParam = index;
    if (index >= ListView_GetItemCount(g_list)) ListView_InsertItem(g_list, &item);
    else ListView_SetItem(g_list, &item);

    ListView_SetItemText(g_list, index, 1, raw_title[0] ? raw_title : L"(조회 중)");
    ListView_SetItemText(g_list, index, 2, clean_name[0] ? clean_name : L"-");
    ListView_SetItemText(g_list, index, 3, size);
    ListView_SetItemText(g_list, index, 4, state);
}

static void RebuildList(void) {
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_list);
    EnterCriticalSection(&g_jobs_lock);
    int count = g_job_count;
    LeaveCriticalSection(&g_jobs_lock);
    for (int i = 0; i < count; ++i) UpdateListRow(i);
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
}

static void UpdatePreviewFromSelection(void) {
    int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (sel < 0) {
        SetControlText(g_raw_title, L"");
        SetControlText(g_clean_title, L"");
        return;
    }
    wchar_t raw_title[1024], clean_name[CLEAN_NAME_CCH], error[768];
    JobStatus status;
    EnterCriticalSection(&g_jobs_lock);
    if (sel >= g_job_count) {
        LeaveCriticalSection(&g_jobs_lock);
        SetControlText(g_raw_title, L"");
        SetControlText(g_clean_title, L"");
        return;
    }
    StringCchCopyW(raw_title, 1024, g_jobs[sel].raw_title);
    StringCchCopyW(clean_name, CLEAN_NAME_CCH, g_jobs[sel].clean_name);
    StringCchCopyW(error, 768, g_jobs[sel].error);
    status = g_jobs[sel].status;
    LeaveCriticalSection(&g_jobs_lock);
    SetControlText(g_raw_title, raw_title);
    SetControlText(g_clean_title, clean_name);
    if (status == JOB_FAILED && error[0] &&
        !InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) &&
        !InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) {
        wchar_t text[900];
        StringCchPrintfW(text, 900, L"상태: 실패 - %s", error);
        SetControlText(g_status, text);
    }
}

static void ApplyManualFilename(void) {
    if (InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) ||
        InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) return;
    int selected = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (selected < 0) return;
    if (GetWindowTextLengthW(g_clean_title) >= CLEAN_NAME_CCH - 1) {
        MessageBoxW(g_main, L"파일명이 너무 깁니다.", APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    wchar_t filename[CLEAN_NAME_CCH];
    GetWindowTextW(g_clean_title, filename, CLEAN_NAME_CCH);
    TrimInPlace(filename);
    size_t length = wcslen(filename);
    if (!length || (length == 4 && !_wcsicmp(filename, L".mp3"))) {
        MessageBoxW(g_main, L"파일 이름을 입력해 주세요.", APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    if (length < 4 || _wcsicmp(filename + length - 4, L".mp3")) {
        if (FAILED(StringCchCatW(filename, CLEAN_NAME_CCH, L".mp3"))) {
            MessageBoxW(g_main, L"파일명이 너무 깁니다.", APP_TITLE, MB_OK | MB_ICONWARNING);
            return;
        }
    }
    if (!Filename_IsSafe(filename)) {
        MessageBoxW(g_main,
            L"Windows에서 사용할 수 없는 파일명입니다. 경로 문자와 예약 이름을 제거해 주세요.",
            APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    EnterCriticalSection(&g_jobs_lock);
    if (selected < g_job_count) {
        StringCchCopyW(g_jobs[selected].clean_name, CLEAN_NAME_CCH, filename);
        g_jobs[selected].custom_name = TRUE;
    }
    LeaveCriticalSection(&g_jobs_lock);
    SetWindowTextW(g_clean_title, filename);
    UpdateListRow(selected);
    Logger_Write(L"filename", L"사용자 지정 파일명을 적용했습니다.");
}

static void SetDownloadUiBusy(BOOL busy) {
    EnableWindow(g_url_edit, !busy);
    EnableWindow(GetDlgItem(g_main, IDC_ADD_LINKS), !busy);
    EnableWindow(g_folder_edit, !busy);
    EnableWindow(GetDlgItem(g_main, IDC_FOLDER_BROWSE), !busy);
    EnableWindow(g_clean_title, !busy);
    EnableWindow(g_apply_filename, !busy);
    EnableWindow(g_download_button, !busy);
    EnableWindow(g_cancel_button, busy);

    const UINT menu_ids[] = {
        IDM_FILE_LOAD_TXT, IDM_FILE_BROWSE_FOLDER, IDM_JOB_ADD_LINKS,
        IDM_JOB_REMOVE_DUP, IDM_JOB_DOWNLOAD_ALL, IDM_JOB_RETRY_FAILED,
        IDM_JOB_CLEAR, IDM_JOB_DELETE_SELECTED, IDM_OPT_DEDUP, IDM_OPT_SKIP,
        IDM_OPT_SANITIZE, IDM_OPT_CLEAN, IDM_OPT_SIZE,
        IDM_QUALITY_128, IDM_QUALITY_192, IDM_QUALITY_256, IDM_QUALITY_320,
        IDM_TOOLS_OPEN_HISTORY
    };
    HMENU menu = GetMenu(g_main);
    UINT state = MF_BYCOMMAND | (busy ? MF_GRAYED : MF_ENABLED);
    for (size_t i = 0; menu && i < sizeof(menu_ids) / sizeof(menu_ids[0]); ++i) {
        EnableMenuItem(menu, menu_ids[i], state);
    }
    DrawMenuBar(g_main);
}

static void RequestCancellation(void) {
    if (g_cancel_event) SetEvent(g_cancel_event);
    EnableWindow(g_cancel_button, FALSE);
    SetControlText(g_status, L"상태: 취소 요청 중...");
    Logger_Write(L"cancel", L"사용자가 작업 취소를 요청했습니다.");
}

static void CloseAfterWorkersIfRequested(void) {
    if (!InterlockedCompareExchange((LONG *)&g_close_after_cancel, 0, 0)) return;
    if (InterlockedCompareExchange((LONG *)&g_download_running, 0, 0) ||
        InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) ||
        InterlockedCompareExchange((LONG *)&g_tools_loading, 0, 0)) return;
    DestroyWindow(g_main);
}

static void RecomputeNames(void) {
    EnterCriticalSection(&g_jobs_lock);
    for (int i = 0; i < g_job_count; ++i) {
        if (g_jobs[i].raw_title[0] && !g_jobs[i].custom_name) {
            BuildCleanFilename(&g_jobs[i], g_jobs[i].clean_name, CLEAN_NAME_CCH);
        }
    }
    LeaveCriticalSection(&g_jobs_lock);
    RebuildList();
    UpdatePreviewFromSelection();
}

static void GetFolderStats(const wchar_t *folder, int *count, ULONGLONG *bytes) {
    *count = 0;
    *bytes = 0;
    wchar_t pattern[MAX_PATH];
    StringCchPrintfW(pattern, MAX_PATH, L"%s\\*.mp3", folder);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            (*count)++;
            *bytes += size;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static unsigned __stdcall FolderStatsThread(void *param) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    FolderStatsResult *result = (FolderStatsResult *)param;
    GetFolderStats(result->folder, &result->count, &result->bytes);
    if (!PostMessageW(g_main, WM_APP_FOLDER_STATS, 0, (LPARAM)result)) free(result);
    return 0;
}

static void UpdateFolderStatsUI(void) {
    FolderStatsResult *result = (FolderStatsResult *)calloc(1, sizeof(FolderStatsResult));
    if (!result) return;
    GetWindowTextW(g_folder_edit, result->folder, MAX_PATH);
    result->generation = InterlockedIncrement((LONG *)&g_stats_generation);
    SetControlText(g_folder_stats, L"폴더 정보 계산 중...");
    uintptr_t thread = _beginthreadex(NULL, 0, FolderStatsThread, result, 0, NULL);
    if (thread) {
        CloseHandle((HANDLE)thread);
    } else {
        FolderStatsThread(result);
    }
}

static void ApplyFolderStatsResult(FolderStatsResult *result) {
    if (!result) return;
    wchar_t folder[MAX_PATH];
    GetWindowTextW(g_folder_edit, folder, MAX_PATH);
    if (result->generation != InterlockedCompareExchange((LONG *)&g_stats_generation, 0, 0) ||
        _wcsicmp(folder, result->folder)) {
        free(result);
        return;
    }
    wchar_t size[64], text[256];
    FormatBytes(result->bytes, size, 64);
    StringCchPrintfW(text, 256, L"현재 폴더: %d곡  |  총 용량: %s", result->count, size);
    SetControlText(g_folder_stats, text);
    free(result);
}

static void Utf8ToWide(const char *src, wchar_t *dst, size_t cch) {
    if (!src || !*src) {
        if (cch) dst[0] = 0;
        return;
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, (int)cch);
    if (!n) MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)cch);
    if (cch) dst[cch - 1] = 0;
}

static void MetadataLineCallbackFn(const char *line, void *ctx) {
    MetadataLines *m = (MetadataLines *)ctx;
    wchar_t wline[4096];
    Utf8ToWide(line, wline, 4096);
    if (!wcsncmp(wline, L"META:", 5)) StringCchCopyW(m->meta, 4096, wline + 5);
    if (*wline) StringCchCopyW(m->last, 1024, wline);
}

static int SplitMeta(wchar_t *text, wchar_t **fields, int max_fields) {
    int count = 0;
    wchar_t *p = text;
    size_t sep_len = wcslen(META_SEP);
    while (count < max_fields) {
        fields[count++] = p;
        wchar_t *sep = wcsstr(p, META_SEP);
        if (!sep) break;
        *sep = 0;
        p = sep + sep_len;
    }
    return count;
}

static BOOL FetchMetadataForJob(int index) {
    if (!g_ytdlp[0]) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, L"yt-dlp.exe를 찾을 수 없습니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    wchar_t url[2048];
    EnterCriticalSection(&g_jobs_lock);
    StringCchCopyW(url, 2048, g_jobs[index].url);
    g_jobs[index].status = JOB_FETCHING;
    g_jobs[index].error[0] = 0;
    LeaveCriticalSection(&g_jobs_lock);
    PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);

    wchar_t print_template[512], js_runtime[MAX_PATH + 16];
    wchar_t command[8192];
    if (FAILED(StringCchPrintfW(js_runtime, MAX_PATH + 16, L"deno:%s", g_deno))) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, L"JavaScript 런타임 경로가 너무 깁니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }
    if (FAILED(StringCchPrintfW(print_template, 512,
            L"META:%%(id)s%s%%(title)s%s%%(artist)s%s%%(track)s%s%%(duration)s",
            META_SEP, META_SEP, META_SEP, META_SEP))) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, L"메타데이터 명령을 만들지 못했습니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }
    const wchar_t *const arguments[] = {
        g_ytdlp, L"--ignore-config", L"--no-update", L"--encoding", L"utf-8", L"--no-playlist",
        L"--js-runtimes", js_runtime, L"--socket-timeout", L"30",
        L"--retries", L"3", L"--extractor-retries", L"3",
        L"--skip-download", L"--quiet", L"--no-warnings", L"--print", print_template,
        L"--", url
    };
    if (!CommandLine_Build(command, 8192, arguments, sizeof(arguments) / sizeof(arguments[0]))) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, L"메타데이터 명령이 너무 깁니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    MetadataLines lines;
    ZeroMemory(&lines, sizeof(lines));
    ProcessResult process_result;
    BOOL ran = Process_RunLines(command, MetadataLineCallbackFn, &lines,
                                g_cancel_event, 120000, &process_result);
    if (!ran || process_result.exit_code != 0 || !lines.meta[0]) {
        const wchar_t *failure = process_result.cancelled ? L"사용자가 정보 조회를 취소했습니다." :
                                 process_result.timed_out ? L"영상 정보 조회 시간이 초과되었습니다." :
                                 lines.last[0] ? lines.last : L"영상 정보를 가져오지 못했습니다.";
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, failure);
        LeaveCriticalSection(&g_jobs_lock);
        Logger_Write(L"metadata", failure);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    wchar_t temp[4096];
    StringCchCopyW(temp, 4096, lines.meta);
    wchar_t *f[5] = {0};
    int n = SplitMeta(temp, f, 5);
    if (n < 2) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, L"메타데이터 형식을 해석하지 못했습니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    EnterCriticalSection(&g_jobs_lock);
    StringCchCopyW(g_jobs[index].video_id, 128, f[0] ? f[0] : L"");
    StringCchCopyW(g_jobs[index].raw_title, 1024, f[1] ? f[1] : L"(제목 없음)");
    StringCchCopyW(g_jobs[index].artist, 512, (n > 2 && !IsNA(f[2])) ? f[2] : L"");
    StringCchCopyW(g_jobs[index].track, 512, (n > 3 && !IsNA(f[3])) ? f[3] : L"");
    double duration = (n > 4 && !IsNA(f[4])) ? _wtof(f[4]) : 0.0;
    g_jobs[index].duration_ms = duration > 0.0 ? (ULONGLONG)(duration * 1000.0 + 0.5) : 0;
    int bitrate = (int)InterlockedCompareExchange((LONG *)&g_audio_bitrate, 0, 0);
    g_jobs[index].expected_size = EstimatedMp3Bytes(g_jobs[index].duration_ms, bitrate);
    g_jobs[index].progress = 0;
    g_jobs[index].status = JOB_READY;
    BuildCleanFilename(&g_jobs[index], g_jobs[index].clean_name, CLEAN_NAME_CCH);
    LeaveCriticalSection(&g_jobs_lock);
    PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
    return TRUE;
}

static BOOL SameUrlOrId(const Job *a, const Job *b) {
    if (a->video_id[0] && b->video_id[0] && !_wcsicmp(a->video_id, b->video_id)) return TRUE;
    return !_wcsicmp(a->url, b->url);
}

static void CompactDuplicates(void) {
    EnterCriticalSection(&g_jobs_lock);
    int out = 0;
    for (int i = 0; i < g_job_count; ++i) {
        BOOL dup = FALSE;
        for (int j = 0; j < out; ++j) {
            if (SameUrlOrId(&g_jobs[i], &g_jobs[j])) {
                dup = TRUE;
                break;
            }
        }
        if (!dup) {
            if (out != i) g_jobs[out] = g_jobs[i];
            out++;
        }
    }
    if (out < g_job_count) ZeroMemory(&g_jobs[out], sizeof(Job) * (size_t)(g_job_count - out));
    g_job_count = out;
    LeaveCriticalSection(&g_jobs_lock);
}

static unsigned __stdcall MetadataWorker(void *param) {
    MetaWork *work = (MetaWork *)param;
    for (;;) {
        int index = (int)InterlockedIncrement(&work->next) - 1;
        if (index >= work->end) break;
        if (g_cancel_event && WaitForSingleObject(g_cancel_event, 0) == WAIT_OBJECT_0) {
            EnterCriticalSection(&g_jobs_lock);
            g_jobs[index].status = JOB_FAILED;
            StringCchCopyW(g_jobs[index].error, 768, L"사용자가 정보 조회를 취소했습니다.");
            LeaveCriticalSection(&g_jobs_lock);
            PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        } else {
            FetchMetadataForJob(index);
        }
    }
    return 0;
}

static unsigned __stdcall MetadataThread(void *param) {
    MetaBatch *batch = (MetaBatch *)param;
    MetaWork work;
    work.next = batch->start;
    work.end = batch->end;
    free(batch);

    HANDLE workers[META_WORKER_COUNT - 1];
    DWORD worker_count = 0;
    int job_count = work.end - (int)work.next;
    int worker_limit = MetadataWorkerLimit();
    int child_count = job_count < worker_limit ? job_count - 1 : worker_limit - 1;
    for (int i = 0; i < child_count; ++i) {
        uintptr_t thread = _beginthreadex(NULL, 0, MetadataWorker, &work, 0, NULL);
        if (thread) workers[worker_count++] = (HANDLE)thread;
    }
    MetadataWorker(&work);
    if (worker_count) WaitForMultipleObjects(worker_count, workers, TRUE, INFINITE);
    for (DWORD i = 0; i < worker_count; ++i) CloseHandle(workers[i]);

    if (InterlockedCompareExchange((LONG *)&g_opt_dedup, 0, 0)) CompactDuplicates();
    InterlockedExchange((LONG *)&g_meta_running, 0);
    PostMessageW(g_main, WM_APP_REBUILD_LIST, 0, 0);
    PostMessageW(g_main, WM_APP_META_DONE, 0, 0);
    return 0;
}

static BOOL UrlAlreadyQueuedUnlocked(const wchar_t *url) {
    for (int i = 0; i < g_job_count; ++i) {
        if (!_wcsicmp(g_jobs[i].url, url)) return TRUE;
    }
    return FALSE;
}

static void StartMetadataBatch(int start, int end) {
    if (start >= end) return;
    if (InterlockedCompareExchange((LONG *)&g_meta_running, 1, 0) != 0) return;
    ResetEvent(g_cancel_event);
    MetaBatch *batch = (MetaBatch *)malloc(sizeof(MetaBatch));
    if (!batch) {
        InterlockedExchange((LONG *)&g_meta_running, 0);
        return;
    }
    batch->start = start;
    batch->end = end;
    uintptr_t th = _beginthreadex(NULL, 0, MetadataThread, batch, 0, NULL);
    if (th) CloseHandle((HANDLE)th);
    else {
        free(batch);
        InterlockedExchange((LONG *)&g_meta_running, 0);
    }
}

static void AddUrlsFromEdit(void) {
    if (InterlockedCompareExchange((LONG *)&g_tools_loading, 0, 0)) {
        MessageBoxW(g_main, L"다운로드 구성 요소를 준비하고 있습니다. 잠시만 기다려 주세요.",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!ToolsAvailable()) {
        MessageBoxW(g_main, L"내장 yt-dlp, FFmpeg 또는 Deno를 준비하지 못했습니다.",
                    APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    if (InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) ||
        InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) {
        MessageBoxW(g_main, L"현재 작업이 끝난 뒤 링크를 추가해 주세요.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    int len = GetWindowTextLengthW(g_url_edit);
    if (len <= 0) return;
    wchar_t *text = (wchar_t *)calloc((size_t)len + 2, sizeof(wchar_t));
    if (!text) return;
    GetWindowTextW(g_url_edit, text, len + 1);

    BOOL dedup = InterlockedCompareExchange((LONG *)&g_opt_dedup, 0, 0) ? TRUE : FALSE;
    wchar_t *ctx = NULL;
    wchar_t *line = wcstok_s(text, L"\r\n", &ctx);
    EnterCriticalSection(&g_jobs_lock);
    int start = g_job_count;
    while (line && g_job_count < MAX_JOBS) {
        TrimInPlace(line);
        if (Filename_IsHttpUrl(line) && !wcschr(line, L'\"')) {
            if (!dedup || !UrlAlreadyQueuedUnlocked(line)) {
                Job *j = &g_jobs[g_job_count++];
                ZeroMemory(j, sizeof(*j));
                StringCchCopyW(j->url, 2048, line);
                j->status = JOB_FETCHING;
            }
        }
        line = wcstok_s(NULL, L"\r\n", &ctx);
    }
    int end = g_job_count;
    LeaveCriticalSection(&g_jobs_lock);
    free(text);
    SetWindowTextW(g_url_edit, L"");
    RebuildList();
    StartMetadataBatch(start, end);
}

static wchar_t *ReadTextFile(const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        return NULL;
    }
    DWORD bytes = (DWORD)size.QuadPart;
    unsigned char *buf = (unsigned char *)malloc(bytes + 2);
    if (!buf) {
        CloseHandle(h);
        return NULL;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, bytes, &got, NULL);
    CloseHandle(h);
    if (!ok) {
        free(buf);
        return NULL;
    }
    buf[got] = buf[got + 1] = 0;

    wchar_t *out = NULL;
    if (got >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
        size_t wc = (got - 2) / 2;
        out = (wchar_t *)calloc(wc + 1, sizeof(wchar_t));
        if (out) memcpy(out, buf + 2, wc * sizeof(wchar_t));
    } else if (got >= 2 && buf[0] == 0xFE && buf[1] == 0xFF) {
        size_t wc = (got - 2) / 2;
        out = (wchar_t *)calloc(wc + 1, sizeof(wchar_t));
        if (out) {
            for (size_t i = 0; i < wc; ++i) {
                out[i] = (wchar_t)(((unsigned int)buf[2 + i * 2] << 8) | buf[3 + i * 2]);
            }
        }
    } else {
        int offset = (got >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) ? 3 : 0;
        UINT cp = CP_UTF8;
        int wc = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (char *)buf + offset, got - offset, NULL, 0);
        if (!wc) {
            cp = CP_ACP;
            wc = MultiByteToWideChar(cp, 0, (char *)buf + offset, got - offset, NULL, 0);
        }
        if (wc > 0) {
            out = (wchar_t *)calloc((size_t)wc + 1, sizeof(wchar_t));
            if (out) MultiByteToWideChar(cp, 0, (char *)buf + offset, got - offset, out, wc);
        }
    }
    free(buf);
    return out;
}

static void LoadTxtFile(void) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"텍스트 파일 (*.txt)\0*.txt\0모든 파일 (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    wchar_t *text = ReadTextFile(file);
    if (!text) {
        MessageBoxW(g_main, L"텍스트 파일을 읽지 못했습니다.", APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    SetWindowTextW(g_url_edit, text);
    free(text);
    AddUrlsFromEdit();
}

static void BrowseFolder(void) {
    if (InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) return;
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = g_main;
    bi.lpszTitle = L"MP3를 저장할 폴더를 선택하세요.";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, path)) {
        SetWindowTextW(g_folder_edit, path);
        StringCchCopyW(g_saved_folder, MAX_PATH, path);
        WriteSettingString(L"OutputFolder", path);
        UpdateFolderStatsUI();
    }
    CoTaskMemFree(pidl);
}

static void OpenFolder(void) {
    wchar_t folder[MAX_PATH];
    GetWindowTextW(g_folder_edit, folder, MAX_PATH);
    if (!DirectoryExistsW2(folder)) SHCreateDirectoryExW(g_main, folder, NULL);
    ShellExecuteW(g_main, L"open", folder, NULL, NULL, SW_SHOWNORMAL);
}

static void DeleteSelected(void) {
    if (InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) ||
        InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) return;
    BOOL selected[MAX_JOBS] = {0};
    int item = -1;
    while ((item = ListView_GetNextItem(g_list, item, LVNI_SELECTED)) != -1) {
        if (item < MAX_JOBS) selected[item] = TRUE;
    }
    EnterCriticalSection(&g_jobs_lock);
    int out = 0;
    for (int i = 0; i < g_job_count; ++i) {
        if (!selected[i]) g_jobs[out++] = g_jobs[i];
    }
    if (out < g_job_count) {
        ZeroMemory(&g_jobs[out], sizeof(Job) * (size_t)(g_job_count - out));
    }
    g_job_count = out;
    LeaveCriticalSection(&g_jobs_lock);
    RebuildList();
    UpdatePreviewFromSelection();
}

static BOOL MakeUniqueDestination(const wchar_t *folder, const wchar_t *filename, wchar_t *out, size_t cch) {
    if (FAILED(StringCchPrintfW(out, cch, L"%s\\%s", folder, filename))) return FALSE;
    if (!FileExistsW2(out)) return TRUE;

    wchar_t base[CLEAN_NAME_CCH];
    StringCchCopyW(base, CLEAN_NAME_CCH, filename);
    wchar_t *dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;
    for (int i = 2; i < 1000; ++i) {
        if (FAILED(StringCchPrintfW(out, cch, L"%s\\%s (%d).mp3", folder, base, i))) return FALSE;
        if (!FileExistsW2(out)) return TRUE;
    }
    return FALSE;
}

static BOOL GetTempRoot(wchar_t *out, size_t cch) {
    wchar_t system_temp[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, system_temp);
    if (!length || length >= MAX_PATH ||
        FAILED(StringCchPrintfW(out, cch, L"%sFebius\\Downrush", system_temp))) return FALSE;
    return SHCreateDirectoryExW(NULL, out, NULL) == ERROR_SUCCESS || DirectoryExistsW2(out);
}

static void DeleteTempTree(const wchar_t *directory) {
    if (!directory || !*directory || !DirectoryExistsW2(directory)) return;
    wchar_t pattern[MAX_PATH];
    if (FAILED(StringCchPrintfW(pattern, MAX_PATH, L"%s\\*", directory))) return;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
            wchar_t child[MAX_PATH];
            if (FAILED(StringCchPrintfW(child, MAX_PATH, L"%s\\%s", directory, data.cFileName))) continue;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) DeleteTempTree(child);
                RemoveDirectoryW(child);
            } else {
                SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(directory);
}

static BOOL CreateBatchTempDirectory(wchar_t *out, size_t cch) {
    wchar_t root[MAX_PATH];
    if (!GetTempRoot(root, MAX_PATH)) return FALSE;
    for (DWORD attempt = 0; attempt < 100; ++attempt) {
        if (FAILED(StringCchPrintfW(out, cch, L"%s\\batch-%lu-%llu-%lu", root,
                GetCurrentProcessId(), GetTickCount64(), attempt))) return FALSE;
        if (CreateDirectoryW(out, NULL)) return TRUE;
        if (GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
    }
    return FALSE;
}

static void CleanupOrphanTempRoot(const wchar_t *root) {
    wchar_t pattern[MAX_PATH];
    if (!root || !*root || !DirectoryExistsW2(root) ||
        FAILED(StringCchPrintfW(pattern, MAX_PATH, L"%s\\batch-*", root))) return;
    FILETIME now_file_time;
    GetSystemTimeAsFileTime(&now_file_time);
    ULARGE_INTEGER now;
    now.LowPart = now_file_time.dwLowDateTime;
    now.HighPart = now_file_time.dwHighDateTime;
    const ULONGLONG one_day = 24ULL * 60ULL * 60ULL * 10000000ULL;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(root);
        return;
    }
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        ULARGE_INTEGER modified;
        modified.LowPart = data.ftLastWriteTime.dwLowDateTime;
        modified.HighPart = data.ftLastWriteTime.dwHighDateTime;
        if (now.QuadPart >= modified.QuadPart && now.QuadPart - modified.QuadPart >= one_day) {
            wchar_t child[MAX_PATH];
            if (SUCCEEDED(StringCchPrintfW(child, MAX_PATH, L"%s\\%s", root, data.cFileName))) {
                DeleteTempTree(child);
            }
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    RemoveDirectoryW(root);
}

static void CleanupOrphanTempDirectories(void) {
    wchar_t root[MAX_PATH];
    if (GetTempRoot(root, MAX_PATH)) CleanupOrphanTempRoot(root);

    wchar_t system_temp[MAX_PATH], legacy[MAX_PATH];
    DWORD length = GetTempPathW(MAX_PATH, system_temp);
    if (length && length < MAX_PATH &&
        SUCCEEDED(StringCchPrintfW(legacy, MAX_PATH,
                                   L"%sFebiusYTMP3Downloader", system_temp))) {
        CleanupOrphanTempRoot(legacy);
    }
}

static void DownloadLineCallbackFn(const char *line, void *ctx) {
    DownloadLines *d = (DownloadLines *)ctx;
    const char *p = strstr(line, "PROGRESS:");
    if (p) {
        double v = atof(p + 9);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        int progress = (int)(v + 0.5);
        if (progress == d->last_progress) return;
        d->last_progress = progress;
        EnterCriticalSection(&g_jobs_lock);
        if (d->index >= 0 && d->index < g_job_count) g_jobs[d->index].progress = progress;
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, d->index, 0);
    } else if (*line) {
        wchar_t wline[1024];
        Utf8ToWide(line, wline, 1024);
        StringCchCopyW(d->last, 1024, wline);
    }
}

static BOOL FailDownloadJob(int index, const wchar_t *message) {
    EnterCriticalSection(&g_jobs_lock);
    if (index >= 0 && index < g_job_count) {
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, message);
    }
    LeaveCriticalSection(&g_jobs_lock);
    PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
    return FALSE;
}

static BOOL SnapshotDownloadJob(int index, DownloadJobSnapshot *snapshot) {
    if (!snapshot) return FALSE;
    EnterCriticalSection(&g_jobs_lock);
    if (index < 0 || index >= g_job_count) {
        LeaveCriticalSection(&g_jobs_lock);
        return FALSE;
    }
    StringCchCopyW(snapshot->url, 2048, g_jobs[index].url);
    StringCchCopyW(snapshot->video_id, 128, g_jobs[index].video_id);
    StringCchCopyW(snapshot->clean_name, CLEAN_NAME_CCH, g_jobs[index].clean_name);
    LeaveCriticalSection(&g_jobs_lock);
    return TRUE;
}

static BOOL DownloadOne(int index, const wchar_t *folder, const wchar_t *temp_dir,
                        int bitrate, BOOL skip_existing) {
    DownloadJobSnapshot job;
    if (!SnapshotDownloadJob(index, &job)) return FALSE;

    if (!job.video_id[0]) {
        if (!FetchMetadataForJob(index)) return FALSE;
        if (!SnapshotDownloadJob(index, &job)) return FALSE;
    }

    if (!Filename_IsSafe(job.video_id)) {
        return FailDownloadJob(index, L"영상 ID가 안전한 파일명 형식이 아닙니다.");
    }
    if (!Filename_IsSafe(job.clean_name)) {
        return FailDownloadJob(index,
            L"안전하지 않거나 Windows에서 사용할 수 없는 파일명입니다. 파일명 자동 제거 옵션을 켜 주세요.");
    }

    if (skip_existing && History_ShouldSkip(folder, job.video_id, job.clean_name, bitrate)) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_SKIPPED;
        g_jobs[index].progress = 100;
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return TRUE;
    }

    if (!g_ytdlp[0] || !g_ffmpeg[0] || !g_deno[0]) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768,
            !g_ytdlp[0] ? L"yt-dlp.exe를 찾을 수 없습니다." :
            !g_ffmpeg[0] ? L"ffmpeg.exe를 찾을 수 없습니다." : L"deno.exe를 찾을 수 없습니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    EnterCriticalSection(&g_jobs_lock);
    g_jobs[index].status = JOB_DOWNLOADING;
    g_jobs[index].progress = 0;
    g_jobs[index].error[0] = 0;
    LeaveCriticalSection(&g_jobs_lock);
    PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);

    wchar_t output_template[MAX_PATH * 2];
    if (FAILED(StringCchPrintfW(output_template, MAX_PATH * 2,
                               L"%s\\%s.%d.%%(ext)s", temp_dir, job.video_id, index))) {
        return FailDownloadJob(index, L"임시 다운로드 경로가 너무 깁니다.");
    }

    wchar_t command[8192], js_runtime[MAX_PATH + 16];
    wchar_t audio_quality[16];
    StringCchPrintfW(audio_quality, 16, L"%dK", bitrate);
    if (FAILED(StringCchPrintfW(js_runtime, MAX_PATH + 16, L"deno:%s", g_deno))) {
        return FailDownloadJob(index, L"JavaScript 런타임 경로가 너무 깁니다.");
    }
    const wchar_t *const arguments[] = {
        g_ytdlp, L"--ignore-config", L"--no-update", L"--encoding", L"utf-8", L"--no-playlist",
        L"--js-runtimes", js_runtime, L"--socket-timeout", L"30",
        L"--retries", L"3", L"--fragment-retries", L"3", L"--extractor-retries", L"3",
        L"--no-warnings", L"--newline", L"--no-color", L"--force-overwrites",
        L"-f", L"bestaudio/best", L"-x", L"--audio-format", L"mp3",
        L"--audio-quality", audio_quality, L"--embed-metadata", L"--ffmpeg-location", g_ffmpeg_dir,
        L"--progress-template", L"download:PROGRESS:%(progress._percent_str)s",
        L"-o", output_template, L"--", job.url
    };
    if (!CommandLine_Build(command, 8192, arguments, sizeof(arguments) / sizeof(arguments[0]))) {
        return FailDownloadJob(index, L"다운로드 명령이 너무 깁니다.");
    }

    DownloadLines lines;
    ZeroMemory(&lines, sizeof(lines));
    lines.index = index;
    lines.last_progress = -1;
    ProcessResult process_result;
    BOOL ran = Process_RunLines(command, DownloadLineCallbackFn, &lines,
                                g_cancel_event, 30 * 60 * 1000, &process_result);
    if (!ran || process_result.exit_code != 0) {
        const wchar_t *failure = process_result.cancelled ? L"사용자가 다운로드를 취소했습니다." :
                                 process_result.timed_out ? L"다운로드 시간이 30분을 초과했습니다." :
                                 lines.last[0] ? lines.last : L"다운로드 또는 MP3 변환에 실패했습니다.";
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, failure);
        LeaveCriticalSection(&g_jobs_lock);
        Logger_Write(L"download", failure);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    wchar_t temp_mp3[MAX_PATH];
    if (FAILED(StringCchPrintfW(temp_mp3, MAX_PATH, L"%s\\%s.%d.mp3", temp_dir, job.video_id, index))) {
        return FailDownloadJob(index, L"변환 파일 경로가 너무 깁니다.");
    }
    if (!FileExistsW2(temp_mp3)) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, L"변환된 MP3 파일을 찾지 못했습니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    wchar_t dest[MAX_PATH];
    EnterCriticalSection(&g_file_lock);
    BOOL destination_ok = MakeUniqueDestination(folder, job.clean_name, dest, MAX_PATH);
    BOOL moved = destination_ok;
    if (moved && _wcsicmp(temp_mp3, dest)) {
        moved = MoveFileExW(temp_mp3, dest, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
    }
    DWORD move_error = moved ? ERROR_SUCCESS : GetLastError();
    LeaveCriticalSection(&g_file_lock);
    if (!destination_ok) {
        return FailDownloadJob(index,
            L"저장 파일명을 만들지 못했습니다. 경로 길이와 같은 이름의 파일 수를 확인해 주세요.");
    }
    if (!moved) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchPrintfW(g_jobs[index].error, 768, L"파일 이름 변경에 실패했습니다. 오류 코드: %lu", move_error);
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    const wchar_t *actual_filename = wcsrchr(dest, L'\\');
    actual_filename = actual_filename ? actual_filename + 1 : dest;
    if (!History_Record(folder, job.video_id, actual_filename, bitrate)) {
        Logger_Write(L"history", L"다운로드 기록을 저장하지 못했습니다.");
    }
    EnterCriticalSection(&g_jobs_lock);
    g_jobs[index].status = JOB_DONE;
    g_jobs[index].progress = 100;
    LeaveCriticalSection(&g_jobs_lock);
    PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
    return TRUE;
}

static unsigned __stdcall DownloadWorker(void *param) {
    DownloadWork *work = (DownloadWork *)param;
    for (;;) {
        int slot = (int)InterlockedIncrement(&work->next) - 1;
        if (slot >= work->total) break;
        if (g_cancel_event && WaitForSingleObject(g_cancel_event, 0) == WAIT_OBJECT_0) {
            FailDownloadJob(work->indices[slot], L"사용자가 다운로드를 취소했습니다.");
        } else {
            DownloadOne(work->indices[slot], work->folder, work->temp_dir,
                        work->bitrate, work->skip_existing);
        }
        int done = (int)InterlockedIncrement(&work->done);
        PostMessageW(g_main, WM_APP_OVERALL, done, work->total);
    }
    return 0;
}

static unsigned __stdcall DownloadThread(void *param) {
    DownloadBatch *batch = (DownloadBatch *)param;
    BOOL failed_only = batch->failed_only;
    DownloadWork work;
    ZeroMemory(&work, sizeof(work));
    StringCchCopyW(work.folder, MAX_PATH, batch->folder);
    StringCchCopyW(work.temp_dir, MAX_PATH, batch->temp_dir);
    work.bitrate = batch->bitrate;
    work.skip_existing = batch->skip_existing;
    free(batch);

    EnterCriticalSection(&g_jobs_lock);
    for (int i = 0; i < g_job_count; ++i) {
        JobStatus s = g_jobs[i].status;
        BOOL pick = failed_only ? (s == JOB_FAILED) : (s == JOB_READY || s == JOB_FAILED || s == JOB_SKIPPED);
        if (pick) work.indices[work.total++] = i;
    }
    LeaveCriticalSection(&g_jobs_lock);

    PostMessageW(g_main, WM_APP_OVERALL, 0, work.total);
    HANDLE workers[DOWNLOAD_WORKER_COUNT - 1];
    DWORD worker_count = 0;
    int worker_limit = DownloadWorkerLimit();
    int active_workers = work.total < worker_limit ? work.total : worker_limit;
    int child_count = active_workers > 0 ? active_workers - 1 : 0;
    for (int i = 0; i < child_count; ++i) {
        uintptr_t thread = _beginthreadex(NULL, 0, DownloadWorker, &work, 0, NULL);
        if (thread) workers[worker_count++] = (HANDLE)thread;
    }
    DownloadWorker(&work);
    if (worker_count) WaitForMultipleObjects(worker_count, workers, TRUE, INFINITE);
    for (DWORD i = 0; i < worker_count; ++i) CloseHandle(workers[i]);

    DeleteTempTree(work.temp_dir);
    Logger_Write(L"download", L"다운로드 배치가 종료되었습니다.");
    InterlockedExchange((LONG *)&g_download_running, 0);
    PostMessageW(g_main, WM_APP_DOWNLOAD_DONE, work.total, 0);
    return 0;
}

static void StartDownload(BOOL failed_only) {
    if (InterlockedCompareExchange((LONG *)&g_tools_loading, 0, 0)) {
        MessageBoxW(g_main, L"다운로드 구성 요소를 준비하고 있습니다. 잠시만 기다려 주세요.",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0)) {
        MessageBoxW(g_main, L"링크 정보를 조회 중입니다. 조회가 끝난 뒤 시작해 주세요.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (InterlockedCompareExchange((LONG *)&g_download_running, 1, 0) != 0) return;

    wchar_t folder[MAX_PATH];
    GetWindowTextW(g_folder_edit, folder, MAX_PATH);
    TrimInPlace(folder);
    if (!*folder) {
        InterlockedExchange((LONG *)&g_download_running, 0);
        MessageBoxW(g_main, L"저장 폴더를 지정해 주세요.", APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    if (SHCreateDirectoryExW(g_main, folder, NULL) != ERROR_SUCCESS && !DirectoryExistsW2(folder)) {
        InterlockedExchange((LONG *)&g_download_running, 0);
        MessageBoxW(g_main, L"저장 폴더를 만들 수 없습니다.", APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    StringCchCopyW(g_saved_folder, MAX_PATH, folder);
    WriteSettingString(L"OutputFolder", folder);

    if (!ToolsAvailable()) {
        InterlockedExchange((LONG *)&g_download_running, 0);
        MessageBoxW(g_main,
            L"내장 yt-dlp, FFmpeg 또는 Deno를 준비하지 못했습니다.\r\n\r\n"
            L"프로그램을 다시 실행하거나 최신 공식 릴리스를 받아 주세요.",
            APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    History_EnsureLoaded(folder);

    DownloadBatch *batch = (DownloadBatch *)malloc(sizeof(DownloadBatch));
    if (!batch) {
        InterlockedExchange((LONG *)&g_download_running, 0);
        return;
    }
    batch->failed_only = failed_only;
    batch->bitrate = (int)InterlockedCompareExchange((LONG *)&g_audio_bitrate, 0, 0);
    batch->skip_existing = InterlockedCompareExchange((LONG *)&g_opt_skip, 0, 0) ? TRUE : FALSE;
    StringCchCopyW(batch->folder, MAX_PATH, folder);
    if (!CreateBatchTempDirectory(batch->temp_dir, MAX_PATH)) {
        free(batch);
        InterlockedExchange((LONG *)&g_download_running, 0);
        MessageBoxW(g_main, L"안전한 임시 작업 폴더를 만들 수 없습니다.", APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    ResetEvent(g_cancel_event);
    SetControlText(g_status, L"다운로드 준비 중...");
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    SetDownloadUiBusy(TRUE);
    Logger_Write(L"download", L"다운로드 배치를 시작했습니다.");

    uintptr_t th = _beginthreadex(NULL, 0, DownloadThread, batch, 0, NULL);
    if (th) CloseHandle((HANDLE)th);
    else {
        DeleteTempTree(batch->temp_dir);
        free(batch);
        InterlockedExchange((LONG *)&g_download_running, 0);
        SetDownloadUiBusy(FALSE);
    }
}

static unsigned __stdcall ToolPreparationThread(void *param) {
    (void)param;
    SetStartupPhase(1);
    PrepareBundledTools();
    SetStartupPhase(3);
    RefreshTools();
    BOOL available = ToolsAvailable();
    Logger_Write(L"tools", available ? L"내장 도구 준비를 완료했습니다." :
                                      L"내장 도구 준비에 실패했습니다.");
    InterlockedExchange((LONG *)&g_tools_loading, 0);
    PostMessageW(g_main, WM_APP_TOOLS_READY, available, 0);
    return 0;
}

static void StartToolPreparation(void) {
    if (InterlockedCompareExchange((LONG *)&g_tools_loading, 1, 0) != 0) return;
    uintptr_t thread = _beginthreadex(NULL, 0, ToolPreparationThread, NULL, 0, NULL);
    if (thread) {
        CloseHandle((HANDLE)thread);
    } else {
        ToolPreparationThread(NULL);
    }
}

static HWND AddCtl(DWORD ex, const wchar_t *cls, const wchar_t *text, DWORD style,
                   int x, int y, int w, int h, int id, HWND parent) {
    HWND ctrl = CreateWindowExW(ex, cls, text, style,
                                ScaleUi(x), ScaleUi(y), ScaleUi(w), ScaleUi(h), parent,
                                (HMENU)(INT_PTR)id, g_instance, NULL);
    ApplyClassic(ctrl);
    return ctrl;
}

static void InitListColumns(void) {
    struct Col { const wchar_t *name; int width; } cols[] = {
        { L"번호", 48 }, { L"제목", 285 }, { L"예상 이름", 300 },
        { L"예상 용량", 95 }, { L"상태", 170 }
    };
    for (int i = 0; i < 5; ++i) {
        LVCOLUMNW c;
        ZeroMemory(&c, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = (LPWSTR)cols[i].name;
        c.cx = ScaleUi(cols[i].width);
        c.iSubItem = i;
        ListView_InsertColumn(g_list, i, &c);
    }
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
}

static void SetDefaultFolder(void) {
    if (g_saved_folder[0]) {
        SetWindowTextW(g_folder_edit, g_saved_folder);
        UpdateFolderStatsUI();
        return;
    }
    wchar_t folder[MAX_PATH];
    if (GetDefaultOutputFolder(folder, MAX_PATH)) {
        SHCreateDirectoryExW(g_main, folder, NULL);
        SetWindowTextW(g_folder_edit, folder);
    } else {
        SetWindowTextW(g_folder_edit, L"C:\\Music\\Febius\\Downrush");
    }
    UpdateFolderStatsUI();
}

static void UpdateStatusBarLayout(HWND hwnd) {
    if (!g_status) return;
    SendMessageW(g_status, WM_SIZE, 0, 0);
    RECT client;
    GetClientRect(hwnd, &client);
    int parts[2] = { client.right - ScaleUi(145), -1 };
    SendMessageW(g_status, SB_SETPARTS, 2, (LPARAM)parts);
    wchar_t quality[48];
    int bitrate = (int)InterlockedCompareExchange((LONG *)&g_audio_bitrate, 0, 0);
    StringCchPrintfW(quality, 48, L"MP3 %d kbps", bitrate);
    SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)quality);
}

static void SyncOptionMenuChecks(void) {
    if (!g_options_menu || !g_quality_menu) return;
    struct MenuSetting {
        UINT id;
        volatile LONG *value;
    } settings[] = {
        { IDM_OPT_DEDUP, &g_opt_dedup },
        { IDM_OPT_SKIP, &g_opt_skip },
        { IDM_OPT_SANITIZE, &g_opt_sanitize },
        { IDM_OPT_CLEAN, &g_opt_clean },
        { IDM_OPT_SIZE, &g_opt_size },
        { IDM_OPT_AUTO_UPDATE, &g_auto_update }
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        UINT flags = MF_BYCOMMAND |
            (InterlockedCompareExchange((LONG *)settings[i].value, 0, 0) ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(g_options_menu, settings[i].id, flags);
    }

    int bitrate = (int)InterlockedCompareExchange((LONG *)&g_audio_bitrate, 0, 0);
    UINT selected = bitrate == 128 ? IDM_QUALITY_128 :
                    bitrate == 192 ? IDM_QUALITY_192 :
                    bitrate == 256 ? IDM_QUALITY_256 : IDM_QUALITY_320;
    CheckMenuRadioItem(g_quality_menu, IDM_QUALITY_128, IDM_QUALITY_320,
                       selected, MF_BYCOMMAND);
}

static void RecomputeExpectedSizes(void) {
    int bitrate = (int)InterlockedCompareExchange((LONG *)&g_audio_bitrate, 0, 0);
    EnterCriticalSection(&g_jobs_lock);
    for (int i = 0; i < g_job_count; ++i) {
        g_jobs[i].expected_size = EstimatedMp3Bytes(g_jobs[i].duration_ms, bitrate);
    }
    LeaveCriticalSection(&g_jobs_lock);
    RebuildList();
}

static void SetAudioBitrate(int bitrate) {
    if (!IsSupportedBitrate((DWORD)bitrate)) return;
    if (InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) {
        MessageBoxW(g_main, L"다운로드가 끝난 뒤 음질을 변경해 주세요.",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (InterlockedExchange((LONG *)&g_audio_bitrate, bitrate) != bitrate) {
        WriteSettingDword(L"AudioBitrate", (DWORD)bitrate);
        RecomputeExpectedSizes();
    }
    SyncOptionMenuChecks();
    UpdateStatusBarLayout(g_main);
}

static void ToggleBooleanOption(int id) {
    if (id != IDM_OPT_AUTO_UPDATE &&
        InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) {
        MessageBoxW(g_main, L"다운로드가 끝난 뒤 옵션을 변경해 주세요.",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    volatile LONG *target = NULL;
    const wchar_t *setting_name = NULL;
    BOOL rebuild_names = FALSE, rebuild_list = FALSE;
    switch (id) {
        case IDM_OPT_DEDUP: target = &g_opt_dedup; setting_name = L"RemoveDuplicates"; break;
        case IDM_OPT_SKIP: target = &g_opt_skip; setting_name = L"SkipDownloaded"; break;
        case IDM_OPT_SANITIZE:
            target = &g_opt_sanitize; setting_name = L"SanitizeFilenames"; rebuild_names = TRUE; break;
        case IDM_OPT_CLEAN:
            target = &g_opt_clean; setting_name = L"CleanNames"; rebuild_names = TRUE; break;
        case IDM_OPT_SIZE:
            target = &g_opt_size; setting_name = L"ShowEstimatedSize"; rebuild_list = TRUE; break;
        case IDM_OPT_AUTO_UPDATE:
            target = &g_auto_update; setting_name = L"CheckUpdatesAtStartup"; break;
        default: return;
    }
    LONG current = InterlockedCompareExchange((LONG *)target, 0, 0);
    LONG next = current ? 0 : 1;
    InterlockedExchange((LONG *)target, next);
    WriteSettingDword(setting_name, next ? 1U : 0U);
    SyncOptionMenuChecks();
    if (rebuild_names) RecomputeNames();
    else if (rebuild_list) RebuildList();
}

static HMENU CreateAppMenu(void) {
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU job = CreatePopupMenu();
    HMENU tools = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    g_options_menu = CreatePopupMenu();
    g_quality_menu = CreatePopupMenu();
    if (!bar || !file || !job || !tools || !help || !g_options_menu || !g_quality_menu) return bar;

    AppendMenuW(file, MF_STRING, IDM_FILE_LOAD_TXT, L"텍스트 파일 불러오기...");
    AppendMenuW(file, MF_STRING, IDM_FILE_BROWSE_FOLDER, L"저장 폴더 변경...");
    AppendMenuW(file, MF_STRING, IDM_FILE_OPEN_FOLDER, L"저장 폴더 열기");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, IDM_FILE_EXIT, L"끝내기");

    AppendMenuW(job, MF_STRING, IDM_JOB_ADD_LINKS, L"링크 추가 및 조회");
    AppendMenuW(job, MF_STRING, IDM_JOB_DELETE_SELECTED, L"선택 항목 삭제\tDelete");
    AppendMenuW(job, MF_STRING, IDM_JOB_REMOVE_DUP, L"중복 항목 제거");
    AppendMenuW(job, MF_SEPARATOR, 0, NULL);
    AppendMenuW(job, MF_STRING, IDM_JOB_DOWNLOAD_ALL, L"전체 다운로드 시작");
    AppendMenuW(job, MF_STRING, IDM_JOB_RETRY_FAILED, L"실패 항목 재시도");
    AppendMenuW(job, MF_SEPARATOR, 0, NULL);
    AppendMenuW(job, MF_STRING, IDM_JOB_CLEAR, L"대기 목록 비우기");

    AppendMenuW(g_quality_menu, MF_STRING, IDM_QUALITY_128, L"128 kbps (작은 용량)");
    AppendMenuW(g_quality_menu, MF_STRING, IDM_QUALITY_192, L"192 kbps");
    AppendMenuW(g_quality_menu, MF_STRING, IDM_QUALITY_256, L"256 kbps");
    AppendMenuW(g_quality_menu, MF_STRING, IDM_QUALITY_320, L"320 kbps (최고 음질)");
    AppendMenuW(g_options_menu, MF_POPUP, (UINT_PTR)g_quality_menu, L"MP3 음질");
    AppendMenuW(g_options_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_options_menu, MF_STRING, IDM_OPT_DEDUP, L"중복 링크 자동 제거");
    AppendMenuW(g_options_menu, MF_STRING, IDM_OPT_SKIP, L"이미 다운로드한 곡 건너뛰기");
    AppendMenuW(g_options_menu, MF_STRING, IDM_OPT_SANITIZE, L"파일명 금지 문자 자동 제거");
    AppendMenuW(g_options_menu, MF_STRING, IDM_OPT_CLEAN, L"이름 자동 정리");
    AppendMenuW(g_options_menu, MF_STRING, IDM_OPT_SIZE, L"예상 파일 용량 표시");
    AppendMenuW(g_options_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(g_options_menu, MF_STRING, IDM_OPT_AUTO_UPDATE, L"시작할 때 업데이트 알림");

    AppendMenuW(tools, MF_STRING, IDM_TOOLS_REFRESH_STATS, L"폴더 정보 새로 고침");
    AppendMenuW(tools, MF_STRING, IDM_TOOLS_OPEN_HISTORY, L"다운로드 기록 열기");
    AppendMenuW(tools, MF_STRING, IDM_TOOLS_OPEN_LOG, L"진단 로그 열기");
    AppendMenuW(help, MF_STRING, IDM_HELP_CHECK_UPDATES, L"업데이트 확인...");
    AppendMenuW(help, MF_STRING, IDM_HELP_RELEASES, L"릴리스 정보 보기");
    AppendMenuW(help, MF_SEPARATOR, 0, NULL);
    AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, L"프로그램 정보...");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"파일(&F)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)job, L"작업(&A)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)g_options_menu, L"옵션(&O)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"도구(&T)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"도움말(&H)");
    SyncOptionMenuChecks();
    return bar;
}

static void CreateUi(HWND hwnd) {
    g_font = CreateFontW(-ScaleUi(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         HANGEUL_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"굴림");

    AddCtl(0, L"BUTTON", L"1. 유튜브 링크", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           8, 6, 944, 130, 0, hwnd);
    g_url_edit = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                        18, 25, 924, 70, IDC_URL_EDIT, hwnd);
    AddCtl(0, L"BUTTON", L"링크 추가 및 조회", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
           782, 102, 160, 27, IDC_ADD_LINKS, hwnd);

    AddCtl(0, L"BUTTON", L"2. 다운로드 대기 목록", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           8, 142, 944, 272, 0, hwnd);
    g_list = AddCtl(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                    18, 162, 924, 240, IDC_LIST, hwnd);
    InitListColumns();

    AddCtl(0, L"BUTTON", L"파일명 미리보기", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           8, 420, 944, 75, 0, hwnd);
    AddCtl(0, L"STATIC", L"원본:", WS_CHILD | WS_VISIBLE,
           20, 443, 58, 20, 0, hwnd);
    g_raw_title = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                         82, 439, 860, 23, IDC_RAW_TITLE, hwnd);
    AddCtl(0, L"STATIC", L"정리:", WS_CHILD | WS_VISIBLE,
           20, 470, 58, 20, 0, hwnd);
    g_clean_title = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           82, 466, 735, 23, IDC_CLEAN_TITLE, hwnd);
    g_apply_filename = AddCtl(0, L"BUTTON", L"파일명 적용", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                              825, 465, 117, 25, IDC_APPLY_FILENAME, hwnd);

    AddCtl(0, L"BUTTON", L"3. 저장 및 다운로드", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           8, 501, 944, 112, 0, hwnd);
    AddCtl(0, L"STATIC", L"저장 폴더:", WS_CHILD | WS_VISIBLE,
           20, 523, 68, 20, 0, hwnd);
    g_folder_edit = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           88, 518, 720, 24, IDC_FOLDER_EDIT, hwnd);
    AddCtl(0, L"BUTTON", L"변경", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
           816, 517, 126, 26, IDC_FOLDER_BROWSE, hwnd);
    g_folder_stats = AddCtl(0, L"STATIC", L"현재 폴더: 0곡  |  총 용량: -", WS_CHILD | WS_VISIBLE,
                            20, 555, 300, 22, IDC_FOLDER_STATS, hwnd);
    g_progress = AddCtl(WS_EX_CLIENTEDGE, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                        330, 554, 380, 20, IDC_PROGRESS, hwnd);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g_overall = AddCtl(0, L"STATIC", L"0 / 0", WS_CHILD | WS_VISIBLE | SS_CENTER,
                       718, 554, 65, 22, IDC_OVERALL, hwnd);
    g_download_button = AddCtl(0, L"BUTTON", L"전체 다운로드", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                               765, 546, 120, 34, IDC_DOWNLOAD_ALL, hwnd);
    g_cancel_button = AddCtl(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED,
                             890, 546, 52, 34, IDC_CANCEL_DOWNLOAD, hwnd);

    g_status = AddCtl(0, STATUSCLASSNAMEW, L"준비 완료", WS_CHILD | WS_VISIBLE,
                      0, 0, 0, 0, IDC_STATUS, hwnd);
    UpdateStatusBarLayout(hwnd);
    SetDefaultFolder();
}

static void ClearJobs(void) {
    if (InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) ||
        InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) return;
    if (g_job_count && MessageBoxW(g_main, L"대기 목록을 모두 비우시겠습니까?", APP_TITLE,
                                  MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    EnterCriticalSection(&g_jobs_lock);
    ZeroMemory(g_jobs, sizeof(g_jobs));
    g_job_count = 0;
    LeaveCriticalSection(&g_jobs_lock);
    RebuildList();
    UpdatePreviewFromSelection();
}

static void OpenHistoryFile(void) {
    if (InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) return;
    wchar_t folder[MAX_PATH], path[MAX_PATH];
    GetWindowTextW(g_folder_edit, folder, MAX_PATH);
    if (!DirectoryExistsW2(folder)) SHCreateDirectoryExW(g_main, folder, NULL);
    if (!History_GetPath(folder, path, MAX_PATH)) return;
    FILE *f = _wfopen(path, L"ab");
    if (f) fclose(f);
    History_Invalidate();
    ShellExecuteW(g_main, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

static void OpenLogFile(void) {
    const wchar_t *path = Logger_GetPath();
    if (path && *path) ShellExecuteW(g_main, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

static void OpenWebPage(HWND owner, const wchar_t *url) {
    HINSTANCE opened = ShellExecuteW(owner, L"open", url, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)opened <= 32) {
        MessageBoxW(owner, L"웹 페이지를 열지 못했습니다.", APP_TITLE, MB_OK | MB_ICONERROR);
    }
}

static BOOL ParseSemanticVersion(const wchar_t *text, int parts[3]) {
    if (!text || !parts) return FALSE;
    const wchar_t *cursor = text;
    if (*cursor == L'v' || *cursor == L'V') cursor++;
    for (int i = 0; i < 3; ++i) {
        if (!iswdigit(*cursor)) return FALSE;
        unsigned int value = 0;
        while (iswdigit(*cursor)) {
            value = value * 10U + (unsigned int)(*cursor - L'0');
            if (value > 65535U) return FALSE;
            cursor++;
        }
        parts[i] = (int)value;
        if (i < 2) {
            if (*cursor != L'.') return FALSE;
            cursor++;
        }
    }
    return !*cursor || *cursor == L'-' || *cursor == L'+';
}

static int CompareSemanticVersion(const wchar_t *left, const wchar_t *right) {
    int a[3], b[3];
    if (!ParseSemanticVersion(left, a) || !ParseSemanticVersion(right, b)) return 0;
    for (int i = 0; i < 3; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static BOOL ExtractReleaseTag(const char *json, wchar_t *version, size_t cch) {
    if (!json || !version || cch < 2) return FALSE;
    const char *key = strstr(json, "\"tag_name\"");
    if (!key) return FALSE;
    const char *colon = strchr(key + 10, ':');
    if (!colon) return FALSE;
    const char *first = strchr(colon + 1, '"');
    if (!first) return FALSE;
    first++;
    if (*first == 'v' || *first == 'V') first++;
    const char *last = strchr(first, '"');
    if (!last || last == first || memchr(first, '\\', (size_t)(last - first))) return FALSE;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, first,
                                    (int)(last - first), version, (int)cch - 1);
    if (count <= 0) return FALSE;
    version[count] = 0;
    int parts[3];
    return ParseSemanticVersion(version, parts);
}

static BOOL FetchLatestVersion(wchar_t *version, size_t cch) {
    BOOL ok = FALSE;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    wchar_t user_agent[96];
    StringCchPrintfW(user_agent, 96, L"FebiusDownrush/%s", APP_VERSION_W);

    session = WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto cleanup;
    WinHttpSetTimeouts(session, 3000, 5000, 5000, 10000);
    connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) goto cleanup;
    request = WinHttpOpenRequest(connection, L"GET",
        L"/repos/NokMyo/youtube-dl/releases/latest", NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) goto cleanup;

    const wchar_t *headers =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpAddRequestHeaders(request, headers, (DWORD)-1L,
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) goto cleanup;

    DWORD status = 0, status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) goto cleanup;

    char response[64 * 1024];
    const DWORD response_capacity = (DWORD)sizeof(response);
    DWORD total = 0;
    while (total < response_capacity - 1) {
        DWORD read_bytes = 0;
        if (!WinHttpReadData(request, response + total,
                             response_capacity - 1 - total, &read_bytes)) goto cleanup;
        if (!read_bytes) break;
        total += read_bytes;
    }
    response[total] = 0;
    ok = total > 0 && ExtractReleaseTag(response, version, cch);

cleanup:
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

static unsigned __stdcall UpdateCheckThread(void *param) {
    UpdateResult *result = (UpdateResult *)param;
    if (FetchLatestVersion(result->latest, 32)) {
        result->state = CompareSemanticVersion(APP_VERSION_W, result->latest) < 0
            ? UPDATE_AVAILABLE : UPDATE_CURRENT;
        WriteSettingQword(L"LastSuccessfulUpdateCheck", CurrentFileTimeValue());
    }
    InterlockedExchange((LONG *)&g_update_running, 0);
    if (!PostMessageW(g_main, WM_APP_UPDATE_RESULT, 0, (LPARAM)result)) free(result);
    return 0;
}

static void StartUpdateCheck(BOOL automatic) {
    if (InterlockedCompareExchange((LONG *)&g_update_running, 1, 0) != 0) {
        if (!automatic) {
            MessageBoxW(g_main, L"업데이트를 확인하고 있습니다.", APP_TITLE,
                        MB_OK | MB_ICONINFORMATION);
        }
        return;
    }
    UpdateResult *result = (UpdateResult *)calloc(1, sizeof(UpdateResult));
    if (!result) {
        InterlockedExchange((LONG *)&g_update_running, 0);
        if (!automatic) {
            MessageBoxW(g_main, L"업데이트 확인 작업을 준비하지 못했습니다.", APP_TITLE,
                        MB_OK | MB_ICONERROR);
        }
        return;
    }
    result->automatic = automatic;
    HMENU menu = GetMenu(g_main);
    EnableMenuItem(menu, IDM_HELP_CHECK_UPDATES, MF_BYCOMMAND | MF_GRAYED);
    DrawMenuBar(g_main);
    if (!automatic) SetControlText(g_status, L"상태: 업데이트 확인 중...");
    uintptr_t thread = _beginthreadex(NULL, 0, UpdateCheckThread, result, 0, NULL);
    if (thread) {
        CloseHandle((HANDLE)thread);
        return;
    }
    free(result);
    InterlockedExchange((LONG *)&g_update_running, 0);
    EnableMenuItem(menu, IDM_HELP_CHECK_UPDATES, MF_BYCOMMAND | MF_ENABLED);
    DrawMenuBar(g_main);
    if (!automatic) {
        MessageBoxW(g_main, L"업데이트 확인 작업을 시작하지 못했습니다.", APP_TITLE,
                    MB_OK | MB_ICONERROR);
    }
}

static void FillRectColor(HDC dc, const RECT *rect, COLORREF color) {
    if (!dc || !rect) return;
    COLORREF previous = SetDCBrushColor(dc, color);
    FillRect(dc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, previous);
}

static void FillPolygonColor(HDC dc, const POINT *points, int count, COLORREF color) {
    if (!dc || !points || count < 3) return;
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    COLORREF previous = SetDCBrushColor(dc, color);
    Polygon(dc, points, count);
    SetDCBrushColor(dc, previous);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
}

static void DrawFebiusSymbolMark(HDC dc, int x, int y, int size) {
    if (g_brand_symbol) {
        DrawIconEx(dc, x, y, g_brand_symbol, size, size, 0, NULL, DI_NORMAL);
        return;
    }

    RECT vertical = { x, y, x + size * 25 / 100, y + size * 86 / 100 };
    RECT top = { x, y, x + size * 72 / 100, y + size * 24 / 100 };
    RECT middle = { x, y + size * 37 / 100,
                    x + size * 52 / 100, y + size * 58 / 100 };
    RECT accent = { x + size * 47 / 100, y + size * 58 / 100,
                    x + size * 73 / 100, y + size * 84 / 100 };
    FillRectColor(dc, &vertical, RGB(11, 13, 18));
    FillRectColor(dc, &top, RGB(11, 13, 18));
    FillRectColor(dc, &middle, RGB(11, 13, 18));
    FillRectColor(dc, &accent, RGB(24, 94, 232));
}

static void DrawFebiusLockup(HDC dc, int x, int y, int symbol_size) {
    DrawFebiusSymbolMark(dc, x, y, symbol_size);
    HFONT font = g_splash_brand_font
        ? g_splash_brand_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(18, 21, 28));
    RECT word = { x + symbol_size + ScaleUi(10), y - ScaleUi(2),
                  x + symbol_size + ScaleUi(180), y + symbol_size + ScaleUi(3) };
    DrawTextW(dc, L"Febius", -1, &word,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

static void DrawParallelogram(HDC dc, int x, int y, int width, int height,
                              COLORREF color) {
    int slant = height / 3;
    POINT points[4] = {
        { x + slant, y }, { x + width, y },
        { x + width - slant, y + height }, { x, y + height }
    };
    FillPolygonColor(dc, points, 4, color);
}

static void DrawBrandGeometry(HDC dc, RECT area) {
    int width = area.right - area.left;
    int height = area.bottom - area.top;
    if (!dc || width < 80 || height < 80) return;

    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
    COLORREF previous_pen = SetDCPenColor(dc, RGB(232, 235, 241));
    for (int i = 1; i <= 4; ++i) {
        int x = area.left + width * i / 5;
        MoveToEx(dc, x, area.top, NULL);
        LineTo(dc, x - height / 5, area.bottom);
    }
    for (int i = 1; i <= 4; ++i) {
        int y = area.top + height * i / 5;
        MoveToEx(dc, area.left, y, NULL);
        LineTo(dc, area.right, y);
    }
    SetDCPenColor(dc, previous_pen);
    SelectObject(dc, old_pen);

    int tile_width = width / 4;
    int tile_height = height / 7;
    DrawParallelogram(dc, area.left + width / 4, area.top + height / 8,
                      tile_width, tile_height, RGB(224, 230, 246));
    DrawParallelogram(dc, area.left + width / 2, area.top + height / 8,
                      tile_width, tile_height, RGB(237, 239, 246));
    DrawParallelogram(dc, area.left + width / 5, area.top + height * 2 / 5,
                      tile_width, tile_height, RGB(39, 102, 229));
    DrawParallelogram(dc, area.left + width * 3 / 5, area.top + height / 2,
                      tile_width, tile_height, RGB(190, 232, 227));
    DrawParallelogram(dc, area.left + width * 2 / 3, area.top + height * 3 / 4,
                      tile_width, tile_height, RGB(219, 215, 241));
}

static void DrawDownrushProductMark(HDC dc, int x, int y, int size) {
    if (g_downrush_artwork) {
        DrawIconEx(dc, x, y, g_downrush_artwork,
                   size, size, 0, NULL, DI_NORMAL);
        return;
    }

    RECT f_vertical = { x + size * 4 / 100, y + size * 6 / 100,
                        x + size * 27 / 100, y + size * 76 / 100 };
    RECT f_top = { x + size * 4 / 100, y + size * 6 / 100,
                   x + size * 67 / 100, y + size * 28 / 100 };
    RECT f_middle = { x + size * 4 / 100, y + size * 36 / 100,
                      x + size * 48 / 100, y + size * 55 / 100 };
    FillRectColor(dc, &f_vertical, RGB(10, 12, 17));
    FillRectColor(dc, &f_top, RGB(10, 12, 17));
    FillRectColor(dc, &f_middle, RGB(10, 12, 17));

    int tile_left = x + size * 39 / 100;
    int tile_top = y + size * 43 / 100;
    int tile_right = x + size * 94 / 100;
    int tile_bottom = y + size * 95 / 100;
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    COLORREF previous_brush = SetDCBrushColor(dc, RGB(24, 94, 232));
    RoundRect(dc, tile_left, tile_top, tile_right, tile_bottom,
              size / 12, size / 12);
    SetDCBrushColor(dc, previous_brush);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);

    POINT lightning[7] = {
        { x + size * 43 / 100, y + size * 39 / 100 },
        { x + size * 28 / 100, y + size * 61 / 100 },
        { x + size * 39 / 100, y + size * 59 / 100 },
        { x + size * 33 / 100, y + size * 78 / 100 },
        { x + size * 55 / 100, y + size * 52 / 100 },
        { x + size * 44 / 100, y + size * 54 / 100 },
        { x + size * 48 / 100, y + size * 39 / 100 }
    };
    FillPolygonColor(dc, lightning, 7, RGB(0, 76, 218));

    int arrow_x = tile_left + (tile_right - tile_left) * 66 / 100;
    int stem_half = size / 28;
    RECT stem = { arrow_x - stem_half, tile_top + size * 9 / 100,
                  arrow_x + stem_half, tile_bottom - size * 16 / 100 };
    FillRectColor(dc, &stem, RGB(255, 255, 255));
    POINT arrow[3] = {
        { arrow_x - size * 12 / 100, tile_bottom - size * 20 / 100 },
        { arrow_x + size * 12 / 100, tile_bottom - size * 20 / 100 },
        { arrow_x, tile_bottom - size * 8 / 100 }
    };
    FillPolygonColor(dc, arrow, 3, RGB(255, 255, 255));

    RECT speed_one = { x + size * 22 / 100, y + size * 84 / 100,
                       x + size * 34 / 100, y + size * 87 / 100 };
    RECT speed_two = { x + size * 17 / 100, y + size * 91 / 100,
                       x + size * 36 / 100, y + size * 94 / 100 };
    FillRectColor(dc, &speed_one, RGB(24, 94, 232));
    FillRectColor(dc, &speed_two, RGB(24, 94, 232));
}

static LONG StartupProgressTarget(void) {
    switch (InterlockedCompareExchange((LONG *)&g_startup_phase, 0, 0)) {
        case 1: return 28;
        case 2: return 64;
        case 3: return 86;
        case 4: return 100;
        default: return 16;
    }
}

static const wchar_t *StartupStatusText(void) {
    switch (InterlockedCompareExchange((LONG *)&g_startup_phase, 0, 0)) {
        case 1: return L"필수 도구를 확인하고 있습니다...";
        case 2: return L"처음 실행에 필요한 도구를 준비하고 있습니다...";
        case 3: return L"사용 환경을 확인하고 있습니다...";
        case 4: return L"프로그램을 시작하고 있습니다...";
        default: return L"프로그램을 초기화하고 있습니다...";
    }
}

static LRESULT CALLBACK SplashProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    (void)lParam;
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, IDT_SPLASH_ANIMATE, 90, NULL);
            return 0;
        case WM_TIMER: {
            LONG progress = InterlockedCompareExchange((LONG *)&g_splash_progress, 0, 0);
            LONG target = StartupProgressTarget();
            if (progress < target) {
                LONG next = progress + (target - progress > 2 ? 2 : 1);
                if (next > target) next = target;
                InterlockedExchange((LONG *)&g_splash_progress, next);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_APP_SPLASH_STATUS:
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(hwnd, &paint);
            RECT client;
            GetClientRect(hwnd, &client);
            FillRectColor(dc, &client, RGB(252, 252, 253));
            SetBkMode(dc, TRANSPARENT);

            RECT geometry = {
                client.right - ScaleUi(286), ScaleUi(42),
                client.right - ScaleUi(18), client.bottom - ScaleUi(24)
            };
            DrawBrandGeometry(dc, geometry);
            DrawFebiusLockup(dc, ScaleUi(38), ScaleUi(28), ScaleUi(30));

            HFONT title_font = g_splash_title_font
                ? g_splash_title_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT body_font = g_splash_body_font
                ? g_splash_body_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT small_font = g_splash_small_font
                ? g_splash_small_font : body_font;
            HFONT old_font = (HFONT)SelectObject(dc, body_font);
            SetTextColor(dc, RGB(83, 88, 101));
            RECT series = {ScaleUi(40), ScaleUi(112), ScaleUi(390), ScaleUi(136)};
            DrawTextW(dc, APP_SERIES, -1, &series,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            SelectObject(dc, title_font);
            SetTextColor(dc, RGB(10, 12, 17));
            RECT product = {ScaleUi(38), ScaleUi(139), ScaleUi(405), ScaleUi(205)};
            DrawTextW(dc, L"Downrush", -1, &product,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            SelectObject(dc, body_font);
            wchar_t version_text[64];
            StringCchPrintfW(version_text, 64, L"Version %s", APP_VERSION_W);
            SetTextColor(dc, RGB(83, 88, 101));
            RECT version = {ScaleUi(40), ScaleUi(207), ScaleUi(390), ScaleUi(232)};
            DrawTextW(dc, version_text, -1, &version,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            SetTextColor(dc, RGB(26, 29, 36));
            RECT status = {ScaleUi(40), ScaleUi(274), ScaleUi(390), ScaleUi(299)};
            DrawTextW(dc, StartupStatusText(), -1, &status,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

            RECT progress = {ScaleUi(40), ScaleUi(311), ScaleUi(340), ScaleUi(316)};
            FillRectColor(dc, &progress, RGB(226, 229, 237));
            LONG progress_value = InterlockedCompareExchange(
                (LONG *)&g_splash_progress, 0, 0);
            RECT completed = progress;
            completed.right = completed.left +
                (completed.right - completed.left) * (int)progress_value / 100;
            FillRectColor(dc, &completed, RGB(24, 94, 232));

            DrawDownrushProductMark(dc, client.right - ScaleUi(190),
                                    ScaleUi(140), ScaleUi(112));

            SelectObject(dc, small_font);
            SetTextColor(dc, RGB(112, 117, 128));
            RECT tagline = {ScaleUi(40), client.bottom - ScaleUi(38),
                            client.right - ScaleUi(32), client.bottom - ScaleUi(17)};
            DrawTextW(dc, APP_TAGLINE, -1, &tagline,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dc, old_font);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_CLOSE:
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, IDT_SPLASH_ANIMATE);
            return 0;
        case WM_NCDESTROY:
            if (g_splash == hwnd) g_splash = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL CreateSplashWindow(void) {
    g_splash_title_font = CreateFontW(-ScaleUi(48), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_splash_body_font = CreateFontW(-ScaleUi(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        HANGEUL_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_splash_brand_font = CreateFontW(-ScaleUi(20), 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_splash_small_font = CreateFontW(-ScaleUi(12), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_brand_symbol = (HICON)LoadImageW(g_instance,
        MAKEINTRESOURCEW(IDI_FEBIUS_SYMBOL), IMAGE_ICON,
        ScaleUi(64), ScaleUi(64), LR_DEFAULTCOLOR);
    g_downrush_artwork = (HICON)LoadImageW(g_instance,
        MAKEINTRESOURCEW(IDI_DOWNRUSH_ARTWORK), IMAGE_ICON,
        ScaleUi(256), ScaleUi(256), LR_DEFAULTCOLOR);
    InterlockedExchange((LONG *)&g_splash_progress, 8);
    int width = ScaleUi(640), height = ScaleUi(390);
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_splash = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"FebiusDownrushSplashWin32", APP_TITLE, WS_POPUP | WS_BORDER,
        x, y, width, height, NULL, NULL, g_instance, NULL);
    if (!g_splash) return FALSE;
    g_splash_started = GetTickCount64();
    ShowWindow(g_splash, SW_SHOWNORMAL);
    UpdateWindow(g_splash);
    return TRUE;
}

static void ShowMainAfterSplash(HWND hwnd) {
    if (g_splash) {
        ULONGLONG elapsed = GetTickCount64() - g_splash_started;
        if (elapsed < 250) {
            SetStartupPhase(4);
            SetTimer(hwnd, IDT_SHOW_MAIN, (UINT)(250 - elapsed), NULL);
            return;
        }
        DestroyWindow(g_splash);
    }
    ShowWindow(hwnd, g_main_show_command);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
}

static void CenterDialog(HWND dialog) {
    RECT dialog_rect, owner_rect;
    HWND owner = GetWindow(dialog, GW_OWNER);
    if (!owner || !GetWindowRect(dialog, &dialog_rect) || !GetWindowRect(owner, &owner_rect)) return;
    int width = dialog_rect.right - dialog_rect.left;
    int height = dialog_rect.bottom - dialog_rect.top;
    int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    SetWindowPos(dialog, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void OpenThirdPartyNotices(HWND owner) {
    wchar_t path[MAX_PATH];
    if (g_tools_dir[0] && SUCCEEDED(StringCchPrintfW(path, MAX_PATH,
            L"%s\\THIRD_PARTY_NOTICES.txt", g_tools_dir)) && FileExistsW2(path)) {
        HINSTANCE opened = ShellExecuteW(owner, L"open", path, NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)opened > 32) return;
    }
    MessageBoxW(owner,
        L"포함된 외부 구성 요소\r\n\r\n"
        L"yt-dlp 및 yt-dlp-ejs — Unlicense\r\n"
        L"Deno JavaScript runtime — MIT License\r\n"
        L"FFmpeg Essentials Build — GNU GPL v3\r\n\r\n"
        L"각 구성 요소의 원문 라이선스와 소스 주소는 배포 파일에 포함된 "
        L"THIRD_PARTY_NOTICES.txt에서 확인할 수 있습니다.",
        L"제3자 소프트웨어 고지", MB_OK | MB_ICONINFORMATION);
}

static void DrawAboutCanvas(const DRAWITEMSTRUCT *item) {
    if (!item || !item->hDC) return;
    HDC dc = item->hDC;
    RECT bounds = item->rcItem;
    int width = bounds.right - bounds.left;
    int height = bounds.bottom - bounds.top;
    int split = bounds.left + width * 61 / 100;
    FillRectColor(dc, &bounds, RGB(253, 253, 254));
    RECT right_panel = { split, bounds.top, bounds.right, bounds.bottom };
    FillRectColor(dc, &right_panel, RGB(249, 250, 252));

    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
    COLORREF previous_pen = SetDCPenColor(dc, RGB(218, 221, 228));
    MoveToEx(dc, split, bounds.top, NULL);
    LineTo(dc, split, bounds.bottom);
    SetDCPenColor(dc, previous_pen);
    SelectObject(dc, old_pen);

    RECT geometry = {
        bounds.left + width * 27 / 100, bounds.top + height * 36 / 100,
        split - ScaleUi(12), bounds.bottom - ScaleUi(20)
    };
    DrawBrandGeometry(dc, geometry);
    DrawFebiusLockup(dc, bounds.left + ScaleUi(38),
                     bounds.top + ScaleUi(28), ScaleUi(31));
    SetBkMode(dc, TRANSPARENT);

    HFONT title_font = g_splash_title_font
        ? g_splash_title_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT body_font = g_splash_body_font
        ? g_splash_body_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT small_font = g_splash_small_font
        ? g_splash_small_font : body_font;
    HFONT old_font = (HFONT)SelectObject(dc, body_font);

    SetTextColor(dc, RGB(83, 88, 101));
    RECT series = {
        bounds.left + ScaleUi(40), bounds.top + height * 23 / 100,
        split - ScaleUi(25), bounds.top + height * 29 / 100
    };
    DrawTextW(dc, APP_SERIES, -1, &series,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SelectObject(dc, title_font);
    SetTextColor(dc, RGB(8, 10, 15));
    RECT product = {
        bounds.left + ScaleUi(38), bounds.top + height * 29 / 100,
        split - ScaleUi(20), bounds.top + height * 44 / 100
    };
    DrawTextW(dc, L"Downrush", -1, &product,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    int mark_size = ScaleUi(138);
    DrawDownrushProductMark(dc, bounds.left + ScaleUi(48),
                            bounds.top + height * 51 / 100, mark_size);

    SelectObject(dc, small_font);
    SetTextColor(dc, RGB(102, 107, 119));
    RECT tagline = {
        bounds.left + ScaleUi(40), bounds.bottom - ScaleUi(42),
        split - ScaleUi(22), bounds.bottom - ScaleUi(18)
    };
    DrawTextW(dc, APP_TAGLINE, -1, &tagline,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    SelectObject(dc, body_font);
    int label_left = split + ScaleUi(32);
    int value_right = bounds.right - ScaleUi(30);
    int row_top = bounds.top + ScaleUi(42);
    const wchar_t *labels[] = { L"버전", L"빌드", L"엔진" };
    const wchar_t *values[] = { APP_VERSION_W, L"Release x64", L"yt-dlp · Deno · FFmpeg" };
    for (int i = 0; i < 3; ++i) {
        int top = row_top + ScaleUi(47) * i;
        RECT label = { label_left, top, value_right, top + ScaleUi(25) };
        SetTextColor(dc, RGB(39, 43, 52));
        DrawTextW(dc, labels[i], -1, &label,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT value = { label_left + ScaleUi(72), top,
                       value_right, top + ScaleUi(25) };
        DrawTextW(dc, values[i], -1, &value,
                  DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (i < 2) {
            HGDIOBJ line_old_pen = SelectObject(dc, GetStockObject(DC_PEN));
            COLORREF line_previous = SetDCPenColor(dc, RGB(222, 225, 231));
            int line_y = top + ScaleUi(35);
            MoveToEx(dc, label_left, line_y, NULL);
            LineTo(dc, value_right, line_y);
            SetDCPenColor(dc, line_previous);
            SelectObject(dc, line_old_pen);
        }
    }

    SelectObject(dc, small_font);
    SetTextColor(dc, RGB(86, 91, 102));
    RECT copyright = {
        label_left, bounds.bottom - ScaleUi(48),
        value_right, bounds.bottom - ScaleUi(28)
    };
    DrawTextW(dc, L"© 2026 NokMyo", -1, &copyright,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT family = {
        label_left, bounds.bottom - ScaleUi(29),
        value_right, bounds.bottom - ScaleUi(10)
    };
    DrawTextW(dc, APP_SERIES, -1, &family,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

static void DrawAboutActionButton(const DRAWITEMSTRUCT *item) {
    if (!item || !item->hDC || !item->hwndItem) return;
    HDC dc = item->hDC;
    RECT bounds = item->rcItem;
    BOOL disabled = (item->itemState & ODS_DISABLED) != 0;
    BOOL pressed = (item->itemState & ODS_SELECTED) != 0;
    BOOL focused = (item->itemState & ODS_FOCUS) != 0;
    FillRectColor(dc, &bounds, pressed ? RGB(238, 243, 252) : RGB(253, 253, 254));

    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
    COLORREF previous_pen = SetDCPenColor(dc,
        focused ? RGB(24, 94, 232) : RGB(185, 189, 198));
    Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SetDCPenColor(dc, previous_pen);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);

    const wchar_t *glyph = L"";
    switch (item->CtlID) {
        case IDC_ABOUT_UPDATE: glyph = L"↻"; break;
        case IDC_ABOUT_RELEASES: glyph = L"↗"; break;
        case IDC_ABOUT_LICENSE: glyph = L"▤"; break;
        case IDC_ABOUT_SYSTEM: glyph = L"▣"; break;
    }
    wchar_t label_text[64];
    GetWindowTextW(item->hwndItem, label_text, 64);
    int offset = pressed ? ScaleUi(1) : 0;
    HFONT font = g_splash_body_font
        ? g_splash_body_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? RGB(155, 158, 166) : RGB(30, 34, 42));
    RECT icon_rect = {
        bounds.left + ScaleUi(18) + offset, bounds.top + offset,
        bounds.left + ScaleUi(52) + offset, bounds.bottom + offset
    };
    DrawTextW(dc, glyph, -1, &icon_rect,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT text_rect = {
        bounds.left + ScaleUi(58) + offset, bounds.top + offset,
        bounds.right - ScaleUi(16) + offset, bounds.bottom + offset
    };
    DrawTextW(dc, label_text, -1, &text_rect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, old_font);
    if (focused) {
        RECT focus = bounds;
        InflateRect(&focus, -ScaleUi(4), -ScaleUi(4));
        DrawFocusRect(dc, &focus);
    }
}

static void ShowSystemInformation(HWND owner) {
    MEMORYSTATUSEX memory;
    ZeroMemory(&memory, sizeof(memory));
    memory.dwLength = sizeof(memory);
    BOOL have_memory = GlobalMemoryStatusEx(&memory);
    unsigned long long memory_gb = have_memory
        ? (memory.ullTotalPhys + (1ULL << 30) - 1) >> 30 : 0;

    wchar_t output_folder[MAX_PATH] = L"";
    if (g_folder_edit && IsWindow(g_folder_edit)) {
        GetWindowTextW(g_folder_edit, output_folder, MAX_PATH);
    }
    if (!output_folder[0]) {
        if (g_saved_folder[0]) StringCchCopyW(output_folder, MAX_PATH, g_saved_folder);
        else GetDefaultOutputFolder(output_folder, MAX_PATH);
    }

    wchar_t message[1400];
    if (have_memory) {
        StringCchPrintfW(message, 1400,
            L"Febius Downrush %s\r\n\r\n"
            L"실행 환경\r\nWindows x64 · 논리 프로세서 %lu개 · 메모리 %llu GB\r\n\r\n"
            L"동시 처리\r\n정보 조회 %d개 · 다운로드 %d개\r\n\r\n"
            L"응용 프로그램 데이터\r\n%s\r\n\r\n"
            L"음악 저장 폴더\r\n%s",
            APP_VERSION_W, g_processor_count, memory_gb,
            MetadataWorkerLimit(), DownloadWorkerLimit(),
            g_local_app_dir[0] ? g_local_app_dir : L"준비 중", output_folder);
    } else {
        StringCchPrintfW(message, 1400,
            L"Febius Downrush %s\r\n\r\n"
            L"실행 환경\r\nWindows x64 · 논리 프로세서 %lu개\r\n\r\n"
            L"동시 처리\r\n정보 조회 %d개 · 다운로드 %d개\r\n\r\n"
            L"응용 프로그램 데이터\r\n%s\r\n\r\n"
            L"음악 저장 폴더\r\n%s",
            APP_VERSION_W, g_processor_count,
            MetadataWorkerLimit(), DownloadWorkerLimit(),
            g_local_app_dir[0] ? g_local_app_dir : L"준비 중", output_folder);
    }
    MessageBoxW(owner, message, L"Downrush 시스템 정보",
                MB_OK | MB_ICONINFORMATION);
}

static INT_PTR CALLBACK AboutDialogProc(HWND dialog, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            CenterDialog(dialog);
            return TRUE;
        case WM_DRAWITEM:
            if ((UINT)wParam == IDC_ABOUT_BANNER) {
                DrawAboutCanvas((const DRAWITEMSTRUCT *)lParam);
                return TRUE;
            }
            if ((UINT)wParam == IDC_ABOUT_UPDATE ||
                (UINT)wParam == IDC_ABOUT_RELEASES ||
                (UINT)wParam == IDC_ABOUT_LICENSE ||
                (UINT)wParam == IDC_ABOUT_SYSTEM) {
                DrawAboutActionButton((const DRAWITEMSTRUCT *)lParam);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_ABOUT_UPDATE) {
                EndDialog(dialog, IDC_ABOUT_UPDATE);
                PostMessageW(g_main, WM_COMMAND,
                             MAKEWPARAM(IDM_HELP_CHECK_UPDATES, 0), 0);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_ABOUT_LICENSE) {
                OpenThirdPartyNotices(dialog);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_ABOUT_RELEASES) {
                OpenWebPage(dialog, RELEASES_URL);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_ABOUT_SYSTEM) {
                ShowSystemInformation(dialog);
                return TRUE;
            }
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(dialog, LOWORD(wParam));
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

static void ShowAboutDialog(HWND owner) {
    DialogBoxParamW(g_instance, MAKEINTRESOURCEW(IDD_ABOUT), owner, AboutDialogProc, 0);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_main = hwnd;
            SetMenu(hwnd, CreateAppMenu());
            CreateUi(hwnd);
            SetControlText(g_status, L"다운로드 구성 요소 준비 중...");
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case IDC_ADD_LINKS: AddUrlsFromEdit(); break;
                case IDM_JOB_ADD_LINKS: AddUrlsFromEdit(); break;
                case IDM_FILE_LOAD_TXT: LoadTxtFile(); break;
                case IDM_JOB_REMOVE_DUP:
                    if (!g_meta_running && !g_download_running) {
                        CompactDuplicates();
                        RebuildList();
                    }
                    break;
                case IDC_FOLDER_BROWSE: BrowseFolder(); break;
                case IDM_FILE_BROWSE_FOLDER: BrowseFolder(); break;
                case IDM_FILE_OPEN_FOLDER: OpenFolder(); break;
                case IDC_DOWNLOAD_ALL: StartDownload(FALSE); break;
                case IDC_CANCEL_DOWNLOAD: RequestCancellation(); break;
                case IDC_APPLY_FILENAME: ApplyManualFilename(); break;
                case IDM_JOB_DOWNLOAD_ALL: StartDownload(FALSE); break;
                case IDM_JOB_RETRY_FAILED: StartDownload(TRUE); break;
                case IDM_JOB_DELETE_SELECTED: DeleteSelected(); break;
                case IDM_JOB_CLEAR: ClearJobs(); break;
                case IDM_TOOLS_REFRESH_STATS: UpdateFolderStatsUI(); break;
                case IDM_TOOLS_OPEN_HISTORY: OpenHistoryFile(); break;
                case IDM_TOOLS_OPEN_LOG: OpenLogFile(); break;
                case IDM_HELP_CHECK_UPDATES: StartUpdateCheck(FALSE); break;
                case IDM_HELP_RELEASES: OpenWebPage(hwnd, RELEASES_URL); break;
                case IDM_HELP_ABOUT: ShowAboutDialog(hwnd); break;
                case IDM_FILE_EXIT: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
                case IDM_OPT_DEDUP:
                case IDM_OPT_SKIP:
                case IDM_OPT_SANITIZE:
                case IDM_OPT_CLEAN:
                case IDM_OPT_SIZE:
                case IDM_OPT_AUTO_UPDATE: ToggleBooleanOption(id); break;
                case IDM_QUALITY_128: SetAudioBitrate(128); break;
                case IDM_QUALITY_192: SetAudioBitrate(192); break;
                case IDM_QUALITY_256: SetAudioBitrate(256); break;
                case IDM_QUALITY_320: SetAudioBitrate(320); break;
            }
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR *hdr = (NMHDR *)lParam;
            if (hdr->idFrom == IDC_LIST && hdr->code == LVN_ITEMCHANGED) {
                UpdatePreviewFromSelection();
            } else if (hdr->idFrom == IDC_LIST && hdr->code == LVN_KEYDOWN &&
                       ((NMLVKEYDOWN *)lParam)->wVKey == VK_DELETE) {
                DeleteSelected();
            }
            return 0;
        }

        case WM_APP_JOB_UPDATED:
            UpdateListRow((int)wParam);
            if (ListView_GetNextItem(g_list, -1, LVNI_SELECTED) == (int)wParam) {
                UpdatePreviewFromSelection();
            }
            return 0;

        case WM_APP_REBUILD_LIST:
            RebuildList();
            return 0;

        case WM_APP_META_DONE:
        {
            int ready = 0, failed = 0;
            EnterCriticalSection(&g_jobs_lock);
            for (int i = 0; i < g_job_count; ++i) {
                if (g_jobs[i].status == JOB_READY) ready++;
                else if (g_jobs[i].status == JOB_FAILED) failed++;
            }
            LeaveCriticalSection(&g_jobs_lock);
            wchar_t text[160];
            StringCchPrintfW(text, 160, L"상태: 링크 정보 조회 완료 (대기 %d, 실패 %d)", ready, failed);
            SetControlText(g_status, text);
            CloseAfterWorkersIfRequested();
            return 0;
        }

        case WM_APP_OVERALL: {
            wchar_t text[128];
            int done = (int)wParam;
            int total = (int)lParam;
            StringCchPrintfW(text, 128, L"%d / %d", done, total);
            SetControlText(g_overall, text);
            SendMessageW(g_progress, PBM_SETPOS, total > 0 ? done * 100 / total : 0, 0);
            return 0;
        }

        case WM_APP_DOWNLOAD_DONE:
        {
            int done = 0, skipped = 0, failed = 0;
            EnterCriticalSection(&g_jobs_lock);
            for (int i = 0; i < g_job_count; ++i) {
                if (g_jobs[i].status == JOB_DONE) done++;
                else if (g_jobs[i].status == JOB_SKIPPED) skipped++;
                else if (g_jobs[i].status == JOB_FAILED) failed++;
            }
            LeaveCriticalSection(&g_jobs_lock);
            wchar_t text[180];
            BOOL cancelled = g_cancel_event && WaitForSingleObject(g_cancel_event, 0) == WAIT_OBJECT_0;
            StringCchPrintfW(text, 180,
                cancelled ? L"상태: 작업 취소됨 (완료 %d, 건너뜀 %d, 실패 %d)" :
                            L"상태: 작업 완료 (완료 %d, 건너뜀 %d, 실패 %d)",
                done, skipped, failed);
            SendMessageW(g_progress, PBM_SETPOS, wParam ? 100 : 0, 0);
            SetControlText(g_status, text);
            SetDownloadUiBusy(FALSE);
            UpdateFolderStatsUI();
            CloseAfterWorkersIfRequested();
            return 0;
        }

        case WM_APP_FOLDER_STATS:
            ApplyFolderStatsResult((FolderStatsResult *)lParam);
            return 0;

        case WM_APP_UPDATE_RESULT: {
            UpdateResult *update = (UpdateResult *)lParam;
            BOOL automatic = update ? update->automatic : FALSE;
            HMENU menu = GetMenu(hwnd);
            EnableMenuItem(menu, IDM_HELP_CHECK_UPDATES, MF_BYCOMMAND | MF_ENABLED);
            DrawMenuBar(hwnd);
            if (update && update->state == UPDATE_AVAILABLE) {
                wchar_t message[256];
                StringCchPrintfW(message, 256,
                    L"새 버전 %s을 사용할 수 있습니다.\r\n\r\n"
                    L"현재 버전: %s\r\n최신 버전: %s\r\n\r\n"
                    L"다운로드 페이지를 여시겠습니까?",
                    update->latest, APP_VERSION_W, update->latest);
                if (MessageBoxW(hwnd, message, L"업데이트 확인",
                                MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                    wchar_t url[256];
                    StringCchPrintfW(url, 256,
                        L"https://github.com/NokMyo/youtube-dl/releases/tag/v%s", update->latest);
                    OpenWebPage(hwnd, url);
                }
            } else if (update && update->state == UPDATE_CURRENT && !automatic) {
                wchar_t message[160];
                StringCchPrintfW(message, 160,
                    L"현재 최신 버전을 사용하고 있습니다.\r\n\r\n버전 %s", APP_VERSION_W);
                MessageBoxW(hwnd, message, L"업데이트 확인", MB_OK | MB_ICONINFORMATION);
            } else if ((!update || update->state == UPDATE_ERROR) && !automatic) {
                if (MessageBoxW(hwnd,
                    L"업데이트 정보를 가져오지 못했습니다.\r\n\r\n"
                    L"GitHub 릴리스 페이지에서 직접 확인하시겠습니까?",
                    L"업데이트 확인", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    OpenWebPage(hwnd, RELEASES_URL);
                }
            }
            free(update);
            if (!automatic && !g_meta_running && !g_download_running) {
                SetControlText(g_status, L"준비 완료");
            }
            return 0;
        }

        case WM_APP_TOOLS_READY:
            SetControlText(g_status, wParam ? L"준비 완료" : L"상태: 내장 yt-dlp, FFmpeg 또는 Deno 준비 실패");
            SetStartupPhase(4);
            ShowMainAfterSplash(hwnd);
            if (ShouldCheckUpdatesAutomatically()) SetTimer(hwnd, IDT_AUTO_UPDATE, 1500, NULL);
            CloseAfterWorkersIfRequested();
            return 0;

        case WM_TIMER:
            if (wParam == IDT_SHOW_MAIN) {
                KillTimer(hwnd, IDT_SHOW_MAIN);
                ShowMainAfterSplash(hwnd);
            } else if (wParam == IDT_AUTO_UPDATE) {
                KillTimer(hwnd, IDT_AUTO_UPDATE);
                StartUpdateCheck(TRUE);
            }
            return 0;

        case WM_SIZE:
            UpdateStatusBarLayout(hwnd);
            return 0;

        case WM_CLOSE:
            if (InterlockedCompareExchange((LONG *)&g_download_running, 0, 0) ||
                InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) ||
                InterlockedCompareExchange((LONG *)&g_tools_loading, 0, 0)) {
                if (MessageBoxW(hwnd,
                    L"현재 작업을 취소하고 종료하시겠습니까?\r\n\r\n진행 중인 하위 프로세스도 함께 종료됩니다.",
                    APP_TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    InterlockedExchange((LONG *)&g_close_after_cancel, 1);
                    RequestCancellation();
                }
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_font) DeleteObject(g_font);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL RunHistorySelfTests(void) {
    wchar_t folder[MAX_PATH], song[MAX_PATH], legacy_song[MAX_PATH], history_path[MAX_PATH];
    if (!CreateBatchTempDirectory(folder, MAX_PATH)) return FALSE;
    if (FAILED(StringCchPrintfW(song, MAX_PATH, L"%s\\song.mp3", folder)) ||
        FAILED(StringCchPrintfW(legacy_song, MAX_PATH, L"%s\\legacy.mp3", folder)) ||
        !History_GetPath(folder, history_path, MAX_PATH)) {
        DeleteTempTree(folder);
        return FALSE;
    }

    BOOL ok = FALSE;
    HANDLE file = CreateFileW(song, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto cleanup;
    CloseHandle(file);
    History_Invalidate();
    if (!History_Record(folder, L"test-id", L"song.mp3", 320) ||
        !History_ShouldSkip(folder, L"test-id", L"song.mp3", 320) ||
        History_ShouldSkip(folder, L"test-id", L"song.mp3", 128)) goto cleanup;
    DeleteFileW(song);
    if (History_ShouldSkip(folder, L"test-id", L"song.mp3", 320)) goto cleanup;

    DeleteFileW(history_path);
    History_Invalidate();
    file = CreateFileW(history_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto cleanup;
    const char legacy_record[] = "legacy-id\r\n";
    DWORD written = 0;
    BOOL wrote = WriteFile(file, legacy_record, (DWORD)(sizeof(legacy_record) - 1), &written, NULL);
    CloseHandle(file);
    if (!wrote || written != (DWORD)(sizeof(legacy_record) - 1)) goto cleanup;
    file = CreateFileW(legacy_song, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto cleanup;
    CloseHandle(file);
    if (!History_ShouldSkip(folder, L"legacy-id", L"legacy.mp3", 320) ||
        History_ShouldSkip(folder, L"legacy-id", L"legacy.mp3", 128)) goto cleanup;
    wchar_t *migrated = ReadTextFile(history_path);
    if (!migrated || !wcsstr(migrated, L"# Febius Downrush download history v2") ||
        !wcsstr(migrated, L"legacy-id\t320\t")) {
        free(migrated);
        goto cleanup;
    }
    free(migrated);
    ok = TRUE;

cleanup:
    History_Invalidate();
    DeleteTempTree(folder);
    return ok;
}

static BOOL RunCoreSelfTests(void) {
    const wchar_t *const arguments[] = {
        L"tool.exe", L"plain", L"with space", L"C:\\trailing\\", L"quote\"inside", L""
    };
    wchar_t command[512];
    if (!CommandLine_Build(command, 512, arguments, sizeof(arguments) / sizeof(arguments[0]))) return FALSE;

    int parsed_count = 0;
    LPWSTR *parsed = CommandLineToArgvW(command, &parsed_count);
    if (!parsed) return FALSE;
    BOOL ok = parsed_count == (int)(sizeof(arguments) / sizeof(arguments[0]));
    for (int i = 0; ok && i < parsed_count; ++i) {
        if (wcscmp(parsed[i], arguments[i])) ok = FALSE;
    }
    LocalFree(parsed);
    if (!ok) return FALSE;

    wchar_t escaped[64];
    if (!PowerShell_EscapeSingleQuoted(L"C:\\O'Brien", escaped, 64) ||
        wcscmp(escaped, L"C:\\O''Brien")) return FALSE;

    wchar_t filename[128];
    StringCchCopyW(filename, 128, L"..\\bad:name?. ");
    Filename_Sanitize(filename, 128);
    if (!Filename_IsSafe(filename) || Filename_IsSafe(L"..\\escape.mp3") ||
        Filename_IsSafe(L"CON.mp3") || Filename_IsSafe(L"CON .mp3") ||
        Filename_IsSafe(L"song.mp3:stream")) return FALSE;

    if (!Filename_IsHttpUrl(L"HTTPS://www.youtube.com/watch?v=test") ||
        Filename_IsHttpUrl(L"ftp://example.com/file")) return FALSE;

    if (!PathsEqualIgnoringTrailingSeparators(L"C:\\Music\\YouTubeMP3\\",
                                               L"c:\\music\\YouTubeMP3") ||
        PathsEqualIgnoringTrailingSeparators(L"C:\\Music\\YouTubeMP3",
                                              L"C:\\Music\\Febius\\Downrush")) return FALSE;

    if (MetadataWorkerLimitFor(1) != 1 || MetadataWorkerLimitFor(2) != 1 ||
        MetadataWorkerLimitFor(4) != 2 || MetadataWorkerLimitFor(8) != META_WORKER_COUNT ||
        DownloadWorkerLimitFor(2) != 1 ||
        DownloadWorkerLimitFor(4) != DOWNLOAD_WORKER_COUNT) return FALSE;

    wchar_t cleaned[256];
    Filename_BuildClean(L"가수 - 노래 (공식 뮤비)", L"", L"", TRUE, TRUE, cleaned, 256);
    if (wcscmp(cleaned, L"가수 - 노래.mp3")) return FALSE;
    Filename_BuildClean(L"가수 - 노래 (Live)", L"", L"", TRUE, TRUE, cleaned, 256);
    if (wcscmp(cleaned, L"가수 - 노래 (Live).mp3")) return FALSE;

    int version_parts[3];
    if (!ParseSemanticVersion(L"v1.2.30", version_parts) ||
        version_parts[0] != 1 || version_parts[1] != 2 || version_parts[2] != 30 ||
        ParseSemanticVersion(L"1.2", version_parts) ||
        CompareSemanticVersion(L"1.0.5", L"1.1.0") >= 0 ||
        CompareSemanticVersion(L"2.0.0", L"1.9.9") <= 0) return FALSE;
    wchar_t release_version[32];
    if (!ExtractReleaseTag("{\"tag_name\":\"v1.1.0\"}", release_version, 32) ||
        wcscmp(release_version, L"1.1.0")) return FALSE;
    if (EstimatedMp3Bytes(180000, 320) != 7200000ULL ||
        EstimatedMp3Bytes(180000, 128) != 2880000ULL ||
        EstimatedMp3Bytes(0, 320) != 0 ||
        !IsSupportedBitrate(128) || !IsSupportedBitrate(192) ||
        !IsSupportedBitrate(256) || !IsSupportedBitrate(320) ||
        IsSupportedBitrate(96) || !RunHistorySelfTests()) return FALSE;
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    g_instance = hInstance;
    SYSTEM_INFO system_info;
    GetNativeSystemInfo(&system_info);
    g_processor_count = system_info.dwNumberOfProcessors ? system_info.dwNumberOfProcessors : 1;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) {
        SetProcessDPIAware();
    }
    g_ui_dpi = GetDpiForSystem();
    if (g_ui_dpi < 96 || g_ui_dpi > 480) g_ui_dpi = 96;
    HRESULT co_init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitializeCriticalSection(&g_jobs_lock);
    InitializeCriticalSection(&g_file_lock);
    InitializeCriticalSection(&g_tools_lock);
    int result = 1;
    GetAppDirectory(g_app_dir, MAX_PATH);
    g_cancel_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_cancel_event || !History_Init() || !InitializeAppDataPaths()) goto cleanup;
    Logger_Init(g_local_app_dir);
    wchar_t startup_log[96];
    if (SUCCEEDED(StringCchPrintfW(startup_log, 96, L"Febius Downrush %s을 시작합니다.", APP_VERSION_W))) {
        Logger_Write(L"app", startup_log);
    }
    wchar_t performance_log[128];
    if (SUCCEEDED(StringCchPrintfW(performance_log, 128,
            L"논리 프로세서 %lu개: 정보 조회 %d개, 다운로드 %d개 동시 작업을 사용합니다.",
            g_processor_count, MetadataWorkerLimit(), DownloadWorkerLimit()))) {
        Logger_Write(L"performance", performance_log);
    }
    CleanupOrphanTempDirectories();

    BOOL core_self_test = HasCommandLineSwitch(L"--self-test-core");
    BOOL tool_self_test = HasCommandLineSwitch(L"--self-test-tools");
    if (!core_self_test && !tool_self_test) {
        g_instance_mutex = CreateMutexW(NULL, TRUE, L"Local\\Febius.Downrush.SingleInstance");
        DWORD mutex_error = GetLastError();
        if (!g_instance_mutex || mutex_error == ERROR_ALREADY_EXISTS) {
            MessageBoxW(NULL, L"Febius Downrush가 이미 실행 중입니다.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
            if (g_instance_mutex) {
                CloseHandle(g_instance_mutex);
                g_instance_mutex = NULL;
            }
            result = 0;
            goto cleanup;
        }
    }

    if (core_self_test) {
        result = RunCoreSelfTests() ? 0 : 3;
        goto cleanup;
    }

    if (tool_self_test) {
        PrepareBundledTools();
        RefreshTools();
        wchar_t ffprobe[MAX_PATH];
        BOOL ok = FindTool(L"ffprobe.exe", ffprobe, MAX_PATH) &&
                  ToolExecutableWorks(g_ytdlp, L"--version") &&
                  ToolExecutableWorks(g_ffmpeg, L"-version") &&
                  ToolExecutableWorks(ffprobe, L"-version") &&
                  ToolExecutableWorks(g_deno, L"--version");
        result = ok ? 0 : 2;
        goto cleanup;
    }

    LoadSettings();
    MigrateLegacyDefaultOutputFolder();

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW splash_wc;
    ZeroMemory(&splash_wc, sizeof(splash_wc));
    splash_wc.cbSize = sizeof(splash_wc);
    splash_wc.style = CS_DROPSHADOW;
    splash_wc.lpfnWndProc = SplashProc;
    splash_wc.hInstance = hInstance;
    splash_wc.hCursor = LoadCursorW(NULL, IDC_WAIT);
    splash_wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    splash_wc.lpszClassName = L"FebiusDownrushSplashWin32";
    if (!RegisterClassExW(&splash_wc)) goto cleanup;

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"FebiusDownrushClassicWin32";
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON),
                                   IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!RegisterClassExW(&wc)) goto cleanup;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, ScaleUi(APP_CLIENT_W), ScaleUi(APP_CLIENT_H)};
    AdjustWindowRect(&r, style, TRUE);
    int width = r.right - r.left;
    int height = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, style,
                                x, y, width, height, NULL, NULL, hInstance, NULL);
    if (!hwnd) goto cleanup;
    g_main_show_command = nCmdShow;
    if (!CreateSplashWindow()) {
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
    }
    StartToolPreparation();

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    for (;;) {
        BOOL message_result = GetMessageW(&msg, NULL, 0, 0);
        if (message_result == 0) {
            result = (int)msg.wParam;
            break;
        }
        if (message_result == (BOOL)-1) {
            result = 1;
            break;
        }
        if (!g_main || !IsDialogMessageW(g_main, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

cleanup:
    if (g_splash && IsWindow(g_splash)) DestroyWindow(g_splash);
    if (g_splash_title_font) DeleteObject(g_splash_title_font);
    if (g_splash_body_font) DeleteObject(g_splash_body_font);
    if (g_splash_brand_font) DeleteObject(g_splash_brand_font);
    if (g_splash_small_font) DeleteObject(g_splash_small_font);
    if (g_brand_symbol) DestroyIcon(g_brand_symbol);
    if (g_downrush_artwork) DestroyIcon(g_downrush_artwork);
    History_Shutdown();
    DeleteCriticalSection(&g_file_lock);
    DeleteCriticalSection(&g_jobs_lock);
    DeleteCriticalSection(&g_tools_lock);
    if (g_cancel_event) CloseHandle(g_cancel_event);
    if (g_instance_mutex) {
        ReleaseMutex(g_instance_mutex);
        CloseHandle(g_instance_mutex);
    }
    Logger_Shutdown();
    if (SUCCEEDED(co_init)) CoUninitialize();
    return result;
}
