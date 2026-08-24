#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")

#define APP_TITLE L"Seowol YT MP3 Downloader"
#define MAX_JOBS 512
#define META_SEP L"<<<YTMP3>>>"
#define APP_CLIENT_W 1220
#define APP_CLIENT_H 820

#define IDC_URL_EDIT            1001
#define IDC_ADD_LINKS           1002
#define IDC_LOAD_TXT            1003
#define IDC_REMOVE_DUP          1004
#define IDC_LIST                1005
#define IDC_CHK_DEDUP           1010
#define IDC_CHK_SKIP            1011
#define IDC_CHK_SANITIZE        1012
#define IDC_CHK_CLEAN           1013
#define IDC_CHK_SIZE            1014
#define IDC_FOLDER_EDIT         1020
#define IDC_FOLDER_BROWSE       1021
#define IDC_FOLDER_OPEN         1022
#define IDC_FOLDER_STATS        1023
#define IDC_DOWNLOAD_ALL        1030
#define IDC_RETRY_FAILED        1031
#define IDC_DELETE_SELECTED     1032
#define IDC_EXIT                1033
#define IDC_PROGRESS            1040
#define IDC_STATUS              1041
#define IDC_OVERALL             1042
#define IDC_RAW_TITLE           1050
#define IDC_CLEAN_TITLE         1051

#define WM_APP_JOB_UPDATED      (WM_APP + 1)
#define WM_APP_REBUILD_LIST     (WM_APP + 2)
#define WM_APP_META_DONE        (WM_APP + 3)
#define WM_APP_CURRENT_JOB      (WM_APP + 4)
#define WM_APP_CURRENT_PROGRESS (WM_APP + 5)
#define WM_APP_OVERALL          (WM_APP + 6)
#define WM_APP_DOWNLOAD_DONE    (WM_APP + 7)
#define WM_APP_FOLDER_STATS     (WM_APP + 8)

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
    wchar_t clean_name[768];
    wchar_t error[768];
    ULONGLONG expected_size;
    int progress;
    JobStatus status;
} Job;

typedef struct MetaBatch {
    int start;
    int end;
} MetaBatch;

typedef struct DownloadBatch {
    BOOL failed_only;
    wchar_t folder[MAX_PATH];
} DownloadBatch;

typedef struct MetadataLines {
    wchar_t meta[4096];
    wchar_t last[1024];
} MetadataLines;

typedef struct DownloadLines {
    int index;
    wchar_t last[1024];
} DownloadLines;

static HINSTANCE g_instance;
static HWND g_main;
static HWND g_url_edit;
static HWND g_list;
static HWND g_folder_edit;
static HWND g_folder_stats;
static HWND g_progress;
static HWND g_status;
static HWND g_overall;
static HWND g_raw_title;
static HWND g_clean_title;
static HWND g_chk_dedup;
static HWND g_chk_skip;
static HWND g_chk_sanitize;
static HWND g_chk_clean;
static HWND g_chk_size;
static HFONT g_font;

static Job g_jobs[MAX_JOBS];
static int g_job_count = 0;
static CRITICAL_SECTION g_jobs_lock;
static volatile LONG g_meta_running = 0;
static volatile LONG g_download_running = 0;
static volatile LONG g_opt_dedup = 1;
static volatile LONG g_opt_skip = 1;
static volatile LONG g_opt_sanitize = 1;
static volatile LONG g_opt_clean = 1;
static volatile LONG g_opt_size = 1;

static wchar_t g_app_dir[MAX_PATH];
static wchar_t g_ytdlp[MAX_PATH];
static wchar_t g_ffmpeg[MAX_PATH];
static wchar_t g_ffmpeg_dir[MAX_PATH];
static wchar_t g_tools_dir[MAX_PATH];

static BOOL FileExistsW2(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL DirectoryExistsW2(const wchar_t *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
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
    DWORD n = SearchPathW(NULL, name, NULL, (DWORD)cch, out, NULL);
    return n > 0 && n < cch;
}

static void DirectoryFromPath(const wchar_t *path, wchar_t *out, size_t cch) {
    StringCchCopyW(out, cch, path);
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash) *slash = L'\0';
}

static BOOL AppendCommandChar(wchar_t *command, size_t cch, size_t *length, wchar_t value) {
    if (*length + 1 >= cch) return FALSE;
    command[(*length)++] = value;
    command[*length] = 0;
    return TRUE;
}

/*
 * Quote one argument according to the CommandLineToArgvW/MSVC parsing rules.
 * Building the command from arguments avoids malformed paths (especially a
 * trailing backslash) and prevents editable text from becoming a yt-dlp option.
 */
static BOOL AppendCommandArgument(wchar_t *command, size_t cch, const wchar_t *argument) {
    if (!command || !argument || !cch) return FALSE;
    size_t length = wcslen(command);
    if (length >= cch) return FALSE;
    if (length && !AppendCommandChar(command, cch, &length, L' ')) return FALSE;
    if (!AppendCommandChar(command, cch, &length, L'"')) return FALSE;

    size_t backslashes = 0;
    for (const wchar_t *p = argument;; ++p) {
        if (*p == L'\\') {
            backslashes++;
            continue;
        }

        size_t copies = *p == L'"' ? backslashes * 2 + 1 : backslashes;
        if (!*p) copies = backslashes * 2;
        for (size_t i = 0; i < copies; ++i) {
            if (!AppendCommandChar(command, cch, &length, L'\\')) return FALSE;
        }
        backslashes = 0;

        if (!*p) break;
        if (!AppendCommandChar(command, cch, &length, *p)) return FALSE;
    }

    return AppendCommandChar(command, cch, &length, L'"');
}

static BOOL BuildCommandLine(wchar_t *command, size_t cch,
                             const wchar_t *const *arguments, size_t argument_count) {
    if (!command || !cch || !arguments || !argument_count) return FALSE;
    command[0] = 0;
    for (size_t i = 0; i < argument_count; ++i) {
        if (!AppendCommandArgument(command, cch, arguments[i])) {
            command[0] = 0;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL EscapePowerShellSingleQuoted(const wchar_t *input, wchar_t *output, size_t cch) {
    if (!input || !output || !cch) return FALSE;
    size_t written = 0;
    for (size_t i = 0; input[i]; ++i) {
        if (written + (input[i] == L'\'' ? 2 : 1) >= cch) return FALSE;
        output[written++] = input[i];
        if (input[i] == L'\'') output[written++] = L'\'';
    }
    output[written] = 0;
    return TRUE;
}

static BOOL RunHiddenProcess(const wchar_t *command) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    wchar_t *cmd = _wcsdup(command);
    if (!cmd) return FALSE;
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) return FALSE;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

static BOOL BundledToolFilesExist(void) {
    if (!g_tools_dir[0]) return FALSE;
    wchar_t p1[MAX_PATH], p2[MAX_PATH], p3[MAX_PATH];
    StringCchPrintfW(p1, MAX_PATH, L"%s\\yt-dlp.exe", g_tools_dir);
    StringCchPrintfW(p2, MAX_PATH, L"%s\\ffmpeg.exe", g_tools_dir);
    StringCchPrintfW(p3, MAX_PATH, L"%s\\ffprobe.exe", g_tools_dir);
    return FileExistsW2(p1) && FileExistsW2(p2) && FileExistsW2(p3);
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

static BOOL PrepareBundledTools(void) {
    g_tools_dir[0] = 0;

    wchar_t local[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, local))) {
        return FALSE;
    }
    StringCchPrintfW(g_tools_dir, MAX_PATH, L"%s\\SeowolYTMP3Downloader\\tools", local);
    SHCreateDirectoryExW(NULL, g_tools_dir, NULL);

    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return FALSE;
    HANDLE in = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (in == INVALID_HANDLE_VALUE) return FALSE;

    LARGE_INTEGER total;
    if (!GetFileSizeEx(in, &total) || total.QuadPart < 16) {
        CloseHandle(in);
        return FALSE;
    }

    LARGE_INTEGER footer_pos;
    footer_pos.QuadPart = -16;
    if (!SetFilePointerEx(in, footer_pos, NULL, FILE_END)) {
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
    if (!payload_size || payload_size > (ULONGLONG)total.QuadPart - 16ULL) {
        CloseHandle(in);
        return FALSE;
    }

    if (BundledToolFilesExist() && ReadBundleStamp((ULONGLONG)total.QuadPart)) {
        CloseHandle(in);
        return TRUE;
    }

    wchar_t temp_dir[MAX_PATH], zip_path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp_dir)) {
        CloseHandle(in);
        return FALSE;
    }
    if (!GetTempFileNameW(temp_dir, L"SYT", 0, zip_path)) {
        CloseHandle(in);
        return FALSE;
    }
    HANDLE out = CreateFileW(zip_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (out == INVALID_HANDLE_VALUE) {
        CloseHandle(in);
        DeleteFileW(zip_path);
        return FALSE;
    }

    LARGE_INTEGER start;
    start.QuadPart = total.QuadPart - 16 - (LONGLONG)payload_size;
    if (!SetFilePointerEx(in, start, NULL, FILE_BEGIN)) {
        CloseHandle(out);
        CloseHandle(in);
        DeleteFileW(zip_path);
        return FALSE;
    }

    unsigned char *buffer = (unsigned char *)malloc(64 * 1024);
    if (!buffer) {
        CloseHandle(out);
        CloseHandle(in);
        DeleteFileW(zip_path);
        return FALSE;
    }

    ULONGLONG remaining = payload_size;
    BOOL copy_ok = TRUE;
    while (remaining) {
        DWORD want = remaining > 64 * 1024 ? 64 * 1024 : (DWORD)remaining;
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

    wchar_t command[4096];
    const wchar_t *const tar_args[] = { L"tar.exe", L"-xf", zip_path, L"-C", g_tools_dir };
    BOOL extracted = BuildCommandLine(command, 4096, tar_args, sizeof(tar_args) / sizeof(tar_args[0])) &&
                     RunHiddenProcess(command);
    if (!extracted) {
        wchar_t escaped_zip[MAX_PATH * 2];
        wchar_t escaped_tools[MAX_PATH * 2];
        wchar_t script[2048];
        if (EscapePowerShellSingleQuoted(zip_path, escaped_zip, MAX_PATH * 2) &&
            EscapePowerShellSingleQuoted(g_tools_dir, escaped_tools, MAX_PATH * 2) &&
            SUCCEEDED(StringCchPrintfW(script, 2048,
                L"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force",
                escaped_zip, escaped_tools))) {
            const wchar_t *const powershell_args[] = {
                L"powershell.exe", L"-NoProfile", L"-NonInteractive",
                L"-ExecutionPolicy", L"Bypass", L"-Command", script
            };
            extracted = BuildCommandLine(command, 4096, powershell_args,
                                          sizeof(powershell_args) / sizeof(powershell_args[0])) &&
                        RunHiddenProcess(command);
        }
    }
    DeleteFileW(zip_path);

    if (extracted && BundledToolFilesExist()) {
        WriteBundleStamp((ULONGLONG)total.QuadPart);
        return TRUE;
    }
    return FALSE;
}

static void RefreshTools(void) {
    g_ytdlp[0] = 0;
    g_ffmpeg[0] = 0;
    g_ffmpeg_dir[0] = 0;
    FindTool(L"yt-dlp.exe", g_ytdlp, MAX_PATH);
    if (FindTool(L"ffmpeg.exe", g_ffmpeg, MAX_PATH)) {
        DirectoryFromPath(g_ffmpeg, g_ffmpeg_dir, MAX_PATH);
    }
}

static void TrimInPlace(wchar_t *s) {
    if (!s || !*s) return;
    wchar_t *start = s;
    while (*start && iswspace(*start)) start++;
    if (start != s) memmove(s, start, (wcslen(start) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n && iswspace(s[n - 1])) s[--n] = 0;
}

static void CollapseSpaces(wchar_t *s) {
    size_t r = 0, w = 0;
    BOOL prev_space = FALSE;
    while (s[r]) {
        wchar_t c = s[r++];
        BOOL space = iswspace(c) ? TRUE : FALSE;
        if (space) {
            if (!prev_space) s[w++] = L' ';
        } else {
            s[w++] = c;
        }
        prev_space = space;
    }
    s[w] = 0;
    TrimInPlace(s);
}

static wchar_t *WcsIstr(wchar_t *haystack, const wchar_t *needle) {
    if (!needle || !*needle) return haystack;
    size_t n = wcslen(needle);
    for (wchar_t *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && towlower(p[i]) == towlower(needle[i])) i++;
        if (i == n) return p;
    }
    return NULL;
}

static BOOL ContainsCI(const wchar_t *text, const wchar_t *needle) {
    return WcsIstr((wchar_t *)text, needle) != NULL;
}

static void RemovePhraseCI(wchar_t *text, const wchar_t *phrase) {
    size_t n = wcslen(phrase);
    if (!n) return;
    for (;;) {
        wchar_t *p = WcsIstr(text, phrase);
        if (!p) break;
        memmove(p, p + n, (wcslen(p + n) + 1) * sizeof(wchar_t));
    }
}

static BOOL NoiseText(const wchar_t *text) {
    static const wchar_t *keys[] = {
        L"official", L"music video", L"lyric", L"lyrics", L"가사",
        L"official audio", L"visualizer", L"m/v", L" mv", L"audio"
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (ContainsCI(text, keys[i])) return TRUE;
    }
    return FALSE;
}

static void RemoveNoiseBrackets(wchar_t *text, wchar_t open, wchar_t close) {
    wchar_t *p = text;
    while ((p = wcschr(p, open)) != NULL) {
        wchar_t *q = wcschr(p + 1, close);
        if (!q) break;
        size_t len = (size_t)(q - p - 1);
        wchar_t inside[512];
        if (len >= 511) len = 511;
        wcsncpy(inside, p + 1, len);
        inside[len] = 0;
        if (NoiseText(inside)) {
            memmove(p, q + 1, (wcslen(q + 1) + 1) * sizeof(wchar_t));
        } else {
            p = q + 1;
        }
    }
}

static BOOL IsReservedDeviceName(const wchar_t *name) {
    wchar_t base[32];
    size_t i = 0;
    while (name[i] && name[i] != L'.' && i < 31) {
        base[i] = towupper(name[i]);
        i++;
    }
    while (i && iswspace(base[i - 1])) i--;
    base[i] = 0;
    if (!_wcsicmp(base, L"CON") || !_wcsicmp(base, L"PRN") ||
        !_wcsicmp(base, L"AUX") || !_wcsicmp(base, L"NUL")) return TRUE;
    if ((wcsncmp(base, L"COM", 3) == 0 || wcsncmp(base, L"LPT", 3) == 0) &&
        wcslen(base) == 4 && base[3] >= L'1' && base[3] <= L'9') return TRUE;
    return FALSE;
}

static BOOL IsSafeFilename(const wchar_t *name) {
    static const wchar_t *bad = L"<>:\"/\\|?*";
    if (!name || !*name || !wcscmp(name, L".") || !wcscmp(name, L"..")) return FALSE;
    for (size_t i = 0; name[i]; ++i) {
        if (name[i] < 32 || wcschr(bad, name[i])) return FALSE;
    }
    size_t n = wcslen(name);
    if (!n || name[n - 1] == L'.' || name[n - 1] == L' ') return FALSE;
    return !IsReservedDeviceName(name);
}

static BOOL IsHttpUrl(const wchar_t *url) {
    return url && (!_wcsnicmp(url, L"http://", 7) || !_wcsnicmp(url, L"https://", 8));
}

static void SanitizeFilename(wchar_t *s, size_t cch) {
    if (!s || !cch) return;
    if (cch == 1) {
        s[0] = 0;
        return;
    }
    static const wchar_t *bad = L"<>:\"/\\|?*";
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] < 32 || wcschr(bad, s[i])) s[i] = L' ';
    }
    CollapseSpaces(s);
    size_t n = wcslen(s);
    while (n && (s[n - 1] == L'.' || s[n - 1] == L' ')) s[--n] = 0;
    if (IsReservedDeviceName(s)) {
        size_t copy = n < cch - 1 ? n : cch - 2;
        memmove(s + 1, s, copy * sizeof(wchar_t));
        s[0] = L'_';
        s[copy + 1] = 0;
    }
}

static BOOL IsNA(const wchar_t *s) {
    return !s || !*s || !_wcsicmp(s, L"NA") || !_wcsicmp(s, L"None") || !_wcsicmp(s, L"null");
}

static void BuildCleanFilename(const Job *job, wchar_t *out, size_t cch) {
    wchar_t base[1024];
    base[0] = 0;

    if (InterlockedCompareExchange((LONG *)&g_opt_clean, 0, 0)) {
        if (!IsNA(job->artist) && !IsNA(job->track)) {
            StringCchPrintfW(base, 1024, L"%s - %s", job->artist, job->track);
        } else {
            const wchar_t *open = wcschr(job->raw_title, L'「');
            const wchar_t *close = open ? wcschr(open + 1, L'」') : NULL;
            if (open && close && close > open + 1) {
                wchar_t left[512];
                wchar_t middle[512];
                size_t l = (size_t)(open - job->raw_title);
                if (l >= 511) l = 511;
                wcsncpy(left, job->raw_title, l);
                left[l] = 0;
                size_t m = (size_t)(close - open - 1);
                if (m >= 511) m = 511;
                wcsncpy(middle, open + 1, m);
                middle[m] = 0;
                TrimInPlace(left);
                TrimInPlace(middle);
                while (*left && (left[wcslen(left) - 1] == L'-' || left[wcslen(left) - 1] == L'–' || left[wcslen(left) - 1] == L'—')) {
                    left[wcslen(left) - 1] = 0;
                    TrimInPlace(left);
                }
                if (*left && *middle) StringCchPrintfW(base, 1024, L"%s - %s", left, middle);
            }
            if (!*base) StringCchCopyW(base, 1024, job->raw_title);
        }

        static const wchar_t *phrases[] = {
            L"Official Music Video", L"Official Video", L"Official Audio",
            L"Official M/V", L"Official MV", L"Music Video", L"Lyric Video"
        };
        for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i) RemovePhraseCI(base, phrases[i]);
        RemoveNoiseBrackets(base, L'(', L')');
        RemoveNoiseBrackets(base, L'[', L']');
        RemoveNoiseBrackets(base, L'【', L'】');
    } else {
        StringCchCopyW(base, 1024, job->raw_title);
    }

    for (size_t i = 0; base[i]; ++i) {
        if (base[i] == L'–' || base[i] == L'—') base[i] = L'-';
    }
    CollapseSpaces(base);
    while (*base && (base[wcslen(base) - 1] == L'-' || base[wcslen(base) - 1] == L'_' || base[wcslen(base) - 1] == L' ')) {
        base[wcslen(base) - 1] = 0;
        TrimInPlace(base);
    }
    if (!*base) StringCchCopyW(base, 1024, L"untitled");

    if (InterlockedCompareExchange((LONG *)&g_opt_sanitize, 0, 0)) SanitizeFilename(base, 1024);
    if (wcslen(base) > 180) base[180] = 0;
    StringCchPrintfW(out, cch, L"%s.mp3", base);
}

static const wchar_t *StatusText(JobStatus status) {
    switch (status) {
        case JOB_FETCHING: return L"정보 조회";
        case JOB_READY: return L"대기";
        case JOB_DOWNLOADING: return L"다운로드";
        case JOB_DONE: return L"완료";
        case JOB_FAILED: return L"실패";
        case JOB_SKIPPED: return L"이미 받음";
        default: return L"-";
    }
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
    if (h) SetWindowTextW(h, text ? text : L"");
}

static void UpdateListRow(int index) {
    Job job;
    EnterCriticalSection(&g_jobs_lock);
    if (index < 0 || index >= g_job_count) {
        LeaveCriticalSection(&g_jobs_lock);
        return;
    }
    job = g_jobs[index];
    LeaveCriticalSection(&g_jobs_lock);

    wchar_t no[32], size[64], prog[32];
    StringCchPrintfW(no, 32, L"%d", index + 1);
    if (InterlockedCompareExchange((LONG *)&g_opt_size, 0, 0)) FormatBytes(job.expected_size, size, 64);
    else StringCchCopyW(size, 64, L"-");
    StringCchPrintfW(prog, 32, L"%d%%", job.progress);

    LVITEMW item = {0};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = no;
    item.lParam = index;
    if (index >= ListView_GetItemCount(g_list)) ListView_InsertItem(g_list, &item);
    else ListView_SetItem(g_list, &item);

    ListView_SetItemText(g_list, index, 1, job.raw_title[0] ? job.raw_title : L"(조회 중)");
    ListView_SetItemText(g_list, index, 2, job.clean_name[0] ? job.clean_name : L"-");
    ListView_SetItemText(g_list, index, 3, size);
    ListView_SetItemText(g_list, index, 4, (LPWSTR)StatusText(job.status));
    ListView_SetItemText(g_list, index, 5, prog);
}

static void RebuildList(void) {
    ListView_DeleteAllItems(g_list);
    EnterCriticalSection(&g_jobs_lock);
    int count = g_job_count;
    LeaveCriticalSection(&g_jobs_lock);
    for (int i = 0; i < count; ++i) UpdateListRow(i);
}

static void UpdatePreviewFromSelection(void) {
    int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (sel < 0) {
        SetControlText(g_raw_title, L"");
        SetControlText(g_clean_title, L"");
        return;
    }
    Job job;
    EnterCriticalSection(&g_jobs_lock);
    if (sel >= g_job_count) {
        LeaveCriticalSection(&g_jobs_lock);
        SetControlText(g_raw_title, L"");
        SetControlText(g_clean_title, L"");
        return;
    }
    job = g_jobs[sel];
    LeaveCriticalSection(&g_jobs_lock);
    SetControlText(g_raw_title, job.raw_title);
    SetControlText(g_clean_title, job.clean_name);
    if (job.status == JOB_FAILED && job.error[0] &&
        !InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) &&
        !InterlockedCompareExchange((LONG *)&g_download_running, 0, 0)) {
        wchar_t text[900];
        StringCchPrintfW(text, 900, L"상태: 실패 - %s", job.error);
        SetControlText(g_status, text);
    }
}

static void RecomputeNames(void) {
    EnterCriticalSection(&g_jobs_lock);
    for (int i = 0; i < g_job_count; ++i) {
        if (g_jobs[i].raw_title[0]) BuildCleanFilename(&g_jobs[i], g_jobs[i].clean_name, 768);
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

static void UpdateFolderStatsUI(void) {
    wchar_t folder[MAX_PATH];
    GetWindowTextW(g_folder_edit, folder, MAX_PATH);
    int count = 0;
    ULONGLONG bytes = 0;
    GetFolderStats(folder, &count, &bytes);
    wchar_t size[64], text[256];
    FormatBytes(bytes, size, 64);
    StringCchPrintfW(text, 256, L"현재 폴더: %d곡\r\n총 용량: %s", count, size);
    SetControlText(g_folder_stats, text);
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

typedef void (*ProcessLineCallback)(const char *line, void *ctx);

static BOOL RunProcessLines(const wchar_t *command, ProcessLineCallback callback, void *ctx, DWORD *exit_code) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE read_pipe = NULL, write_pipe = NULL;
    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) return FALSE;
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    wchar_t *cmd = _wcsdup(command);
    if (!cmd) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return FALSE;
    }

    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmd);
    CloseHandle(write_pipe);
    if (!ok) {
        CloseHandle(read_pipe);
        return FALSE;
    }

    char chunk[4096];
    char line[16384];
    size_t line_len = 0;
    DWORD got = 0;
    while (ReadFile(read_pipe, chunk, sizeof(chunk), &got, NULL) && got) {
        for (DWORD i = 0; i < got; ++i) {
            char c = chunk[i];
            if (c == '\n') {
                line[line_len] = 0;
                if (callback) callback(line, ctx);
                line_len = 0;
            } else if (c != '\r') {
                if (line_len + 1 < sizeof(line)) line[line_len++] = c;
            }
        }
    }
    if (line_len) {
        line[line_len] = 0;
        if (callback) callback(line, ctx);
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exit_code) *exit_code = code;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
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
    RefreshTools();
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

    wchar_t print_template[512];
    wchar_t command[8192];
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
        g_ytdlp, L"--ignore-config", L"--encoding", L"utf-8", L"--no-playlist",
        L"--skip-download", L"--quiet", L"--no-warnings", L"--print", print_template,
        L"--", url
    };
    if (!BuildCommandLine(command, 8192, arguments, sizeof(arguments) / sizeof(arguments[0]))) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, L"메타데이터 명령이 너무 깁니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    MetadataLines lines;
    ZeroMemory(&lines, sizeof(lines));
    DWORD code = 1;
    BOOL ran = RunProcessLines(command, MetadataLineCallbackFn, &lines, &code);
    if (!ran || code != 0 || !lines.meta[0]) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, lines.last[0] ? lines.last : L"영상 정보를 가져오지 못했습니다.");
        LeaveCriticalSection(&g_jobs_lock);
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
    g_jobs[index].expected_size = duration > 0.0 ? (ULONGLONG)(duration * 320000.0 / 8.0) : 0;
    g_jobs[index].progress = 0;
    g_jobs[index].status = JOB_READY;
    BuildCleanFilename(&g_jobs[index], g_jobs[index].clean_name, 768);
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
    Job *temp = (Job *)calloc(MAX_JOBS, sizeof(Job));
    if (!temp) {
        LeaveCriticalSection(&g_jobs_lock);
        return;
    }
    int out = 0;
    for (int i = 0; i < g_job_count; ++i) {
        BOOL dup = FALSE;
        for (int j = 0; j < out; ++j) {
            if (SameUrlOrId(&g_jobs[i], &temp[j])) {
                dup = TRUE;
                break;
            }
        }
        if (!dup) temp[out++] = g_jobs[i];
    }
    memcpy(g_jobs, temp, sizeof(Job) * out);
    g_job_count = out;
    free(temp);
    LeaveCriticalSection(&g_jobs_lock);
}

static unsigned __stdcall MetadataThread(void *param) {
    MetaBatch *batch = (MetaBatch *)param;
    int start = batch->start;
    int end = batch->end;
    free(batch);

    for (int i = start; i < end; ++i) FetchMetadataForJob(i);
    if (InterlockedCompareExchange((LONG *)&g_opt_dedup, 0, 0)) CompactDuplicates();
    InterlockedExchange((LONG *)&g_meta_running, 0);
    PostMessageW(g_main, WM_APP_REBUILD_LIST, 0, 0);
    PostMessageW(g_main, WM_APP_META_DONE, 0, 0);
    return 0;
}

static BOOL UrlAlreadyQueued(const wchar_t *url) {
    BOOL found = FALSE;
    EnterCriticalSection(&g_jobs_lock);
    for (int i = 0; i < g_job_count; ++i) {
        if (!_wcsicmp(g_jobs[i].url, url)) {
            found = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&g_jobs_lock);
    return found;
}

static void StartMetadataBatch(int start, int end) {
    if (start >= end) return;
    if (InterlockedCompareExchange((LONG *)&g_meta_running, 1, 0) != 0) return;
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

    int start = g_job_count;
    wchar_t *ctx = NULL;
    wchar_t *line = wcstok_s(text, L"\r\n", &ctx);
    while (line && g_job_count < MAX_JOBS) {
        TrimInPlace(line);
        if (IsHttpUrl(line) && !wcschr(line, L'\"')) {
            BOOL dedup = InterlockedCompareExchange((LONG *)&g_opt_dedup, 0, 0) ? TRUE : FALSE;
            if (!dedup || !UrlAlreadyQueued(line)) {
                EnterCriticalSection(&g_jobs_lock);
                Job *j = &g_jobs[g_job_count++];
                ZeroMemory(j, sizeof(*j));
                StringCchCopyW(j->url, 2048, line);
                j->status = JOB_FETCHING;
                LeaveCriticalSection(&g_jobs_lock);
            }
        }
        line = wcstok_s(NULL, L"\r\n", &ctx);
    }
    free(text);
    SetWindowTextW(g_url_edit, L"");
    RebuildList();
    StartMetadataBatch(start, g_job_count);
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
    g_job_count = out;
    LeaveCriticalSection(&g_jobs_lock);
    RebuildList();
    UpdatePreviewFromSelection();
}

static void HistoryPath(const wchar_t *folder, wchar_t *out, size_t cch) {
    StringCchPrintfW(out, cch, L"%s\\download_history.txt", folder);
}

static BOOL HistoryContains(const wchar_t *folder, const wchar_t *id) {
    wchar_t path[MAX_PATH];
    HistoryPath(folder, path, MAX_PATH);
    FILE *f = _wfopen(path, L"rb");
    if (!f) return FALSE;
    char id8[256];
    WideCharToMultiByte(CP_UTF8, 0, id, -1, id8, sizeof(id8), NULL, NULL);
    char line[512];
    BOOL found = FALSE;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = 0;
        if (!_stricmp(line, id8)) {
            found = TRUE;
            break;
        }
    }
    fclose(f);
    return found;
}

static void HistoryAppend(const wchar_t *folder, const wchar_t *id) {
    if (!id[0] || HistoryContains(folder, id)) return;
    wchar_t path[MAX_PATH];
    HistoryPath(folder, path, MAX_PATH);
    FILE *f = _wfopen(path, L"ab");
    if (!f) return;
    char id8[256];
    WideCharToMultiByte(CP_UTF8, 0, id, -1, id8, sizeof(id8), NULL, NULL);
    fprintf(f, "%s\r\n", id8);
    fclose(f);
}

static BOOL MakeUniqueDestination(const wchar_t *folder, const wchar_t *filename, wchar_t *out, size_t cch) {
    if (FAILED(StringCchPrintfW(out, cch, L"%s\\%s", folder, filename))) return FALSE;
    if (!FileExistsW2(out)) return TRUE;

    wchar_t base[768];
    StringCchCopyW(base, 768, filename);
    wchar_t *dot = wcsrchr(base, L'.');
    if (dot) *dot = 0;
    for (int i = 2; i < 1000; ++i) {
        if (FAILED(StringCchPrintfW(out, cch, L"%s\\%s (%d).mp3", folder, base, i))) return FALSE;
        if (!FileExistsW2(out)) return TRUE;
    }
    return FALSE;
}

static void DownloadLineCallbackFn(const char *line, void *ctx) {
    DownloadLines *d = (DownloadLines *)ctx;
    const char *p = strstr(line, "PROGRESS:");
    if (p) {
        double v = atof(p + 9);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        EnterCriticalSection(&g_jobs_lock);
        if (d->index >= 0 && d->index < g_job_count) g_jobs[d->index].progress = (int)(v + 0.5);
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, d->index, 0);
        PostMessageW(g_main, WM_APP_CURRENT_PROGRESS, (WPARAM)(int)(v + 0.5), 0);
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

static BOOL DownloadOne(int index, const wchar_t *folder) {
    Job job;
    EnterCriticalSection(&g_jobs_lock);
    if (index < 0 || index >= g_job_count) {
        LeaveCriticalSection(&g_jobs_lock);
        return FALSE;
    }
    job = g_jobs[index];
    LeaveCriticalSection(&g_jobs_lock);

    if (!job.video_id[0]) {
        if (!FetchMetadataForJob(index)) return FALSE;
        EnterCriticalSection(&g_jobs_lock);
        job = g_jobs[index];
        LeaveCriticalSection(&g_jobs_lock);
    }

    if (!IsSafeFilename(job.video_id)) {
        return FailDownloadJob(index, L"영상 ID가 안전한 파일명 형식이 아닙니다.");
    }
    if (!IsSafeFilename(job.clean_name)) {
        return FailDownloadJob(index,
            L"안전하지 않거나 Windows에서 사용할 수 없는 파일명입니다. 파일명 자동 제거 옵션을 켜 주세요.");
    }

    wchar_t final_path[MAX_PATH];
    if (FAILED(StringCchPrintfW(final_path, MAX_PATH, L"%s\\%s", folder, job.clean_name))) {
        return FailDownloadJob(index, L"저장 경로가 너무 깁니다. 더 짧은 폴더를 선택해 주세요.");
    }
    BOOL skip = InterlockedCompareExchange((LONG *)&g_opt_skip, 0, 0) ? TRUE : FALSE;
    if (skip && (HistoryContains(folder, job.video_id) || FileExistsW2(final_path))) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_SKIPPED;
        g_jobs[index].progress = 100;
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return TRUE;
    }

    RefreshTools();
    if (!g_ytdlp[0] || !g_ffmpeg[0]) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, !g_ytdlp[0] ? L"yt-dlp.exe를 찾을 수 없습니다." : L"ffmpeg.exe를 찾을 수 없습니다.");
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
    PostMessageW(g_main, WM_APP_CURRENT_JOB, index, 0);
    PostMessageW(g_main, WM_APP_CURRENT_PROGRESS, 0, 0);

    wchar_t output_template[MAX_PATH * 2];
    if (FAILED(StringCchPrintfW(output_template, MAX_PATH * 2,
                               L"%s\\%s.%%(ext)s", folder, job.video_id))) {
        return FailDownloadJob(index, L"임시 다운로드 경로가 너무 깁니다.");
    }

    wchar_t command[8192];
    const wchar_t *const arguments[] = {
        g_ytdlp, L"--ignore-config", L"--encoding", L"utf-8", L"--no-playlist",
        L"--no-warnings", L"--newline", L"--no-color", L"--force-overwrites",
        L"-f", L"bestaudio/best", L"-x", L"--audio-format", L"mp3",
        L"--audio-quality", L"320K", L"--ffmpeg-location", g_ffmpeg_dir,
        L"--progress-template", L"download:PROGRESS:%(progress._percent_str)s",
        L"-o", output_template, L"--", job.url
    };
    if (!BuildCommandLine(command, 8192, arguments, sizeof(arguments) / sizeof(arguments[0]))) {
        return FailDownloadJob(index, L"다운로드 명령이 너무 깁니다.");
    }

    DownloadLines lines;
    ZeroMemory(&lines, sizeof(lines));
    lines.index = index;
    DWORD code = 1;
    BOOL ran = RunProcessLines(command, DownloadLineCallbackFn, &lines, &code);
    if (!ran || code != 0) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchCopyW(g_jobs[index].error, 768, lines.last[0] ? lines.last : L"다운로드 또는 MP3 변환에 실패했습니다.");
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    wchar_t temp_mp3[MAX_PATH];
    if (FAILED(StringCchPrintfW(temp_mp3, MAX_PATH, L"%s\\%s.mp3", folder, job.video_id))) {
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
    if (skip) {
        StringCchCopyW(dest, MAX_PATH, final_path);
    } else if (!MakeUniqueDestination(folder, job.clean_name, dest, MAX_PATH)) {
        return FailDownloadJob(index,
            L"저장 파일명을 만들지 못했습니다. 경로 길이와 같은 이름의 파일 수를 확인해 주세요.");
    }

    BOOL moved = TRUE;
    if (_wcsicmp(temp_mp3, dest)) moved = MoveFileExW(temp_mp3, dest, MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
    if (!moved) {
        EnterCriticalSection(&g_jobs_lock);
        g_jobs[index].status = JOB_FAILED;
        StringCchPrintfW(g_jobs[index].error, 768, L"파일 이름 변경에 실패했습니다. 오류 코드: %lu", GetLastError());
        LeaveCriticalSection(&g_jobs_lock);
        PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
        return FALSE;
    }

    HistoryAppend(folder, job.video_id);
    EnterCriticalSection(&g_jobs_lock);
    g_jobs[index].status = JOB_DONE;
    g_jobs[index].progress = 100;
    LeaveCriticalSection(&g_jobs_lock);
    PostMessageW(g_main, WM_APP_JOB_UPDATED, index, 0);
    PostMessageW(g_main, WM_APP_FOLDER_STATS, 0, 0);
    return TRUE;
}

static unsigned __stdcall DownloadThread(void *param) {
    DownloadBatch *batch = (DownloadBatch *)param;
    BOOL failed_only = batch->failed_only;
    wchar_t folder[MAX_PATH];
    StringCchCopyW(folder, MAX_PATH, batch->folder);
    free(batch);

    int indices[MAX_JOBS];
    int total = 0;
    EnterCriticalSection(&g_jobs_lock);
    for (int i = 0; i < g_job_count; ++i) {
        JobStatus s = g_jobs[i].status;
        BOOL pick = failed_only ? (s == JOB_FAILED) : (s == JOB_READY || s == JOB_FAILED || s == JOB_SKIPPED);
        if (pick) indices[total++] = i;
    }
    LeaveCriticalSection(&g_jobs_lock);

    PostMessageW(g_main, WM_APP_OVERALL, 0, total);
    int done = 0;
    for (int k = 0; k < total; ++k) {
        DownloadOne(indices[k], folder);
        done++;
        PostMessageW(g_main, WM_APP_OVERALL, done, total);
    }

    InterlockedExchange((LONG *)&g_download_running, 0);
    PostMessageW(g_main, WM_APP_DOWNLOAD_DONE, 0, 0);
    return 0;
}

static void StartDownload(BOOL failed_only) {
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

    RefreshTools();
    if (!g_ytdlp[0] || !g_ffmpeg[0]) {
        InterlockedExchange((LONG *)&g_download_running, 0);
        MessageBoxW(g_main,
            L"yt-dlp.exe와 ffmpeg.exe가 필요합니다.\r\n\r\n프로그램과 같은 폴더에 두거나 PATH에 등록해 주세요.",
            APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }

    DownloadBatch *batch = (DownloadBatch *)malloc(sizeof(DownloadBatch));
    if (!batch) {
        InterlockedExchange((LONG *)&g_download_running, 0);
        return;
    }
    batch->failed_only = failed_only;
    StringCchCopyW(batch->folder, MAX_PATH, folder);
    SetControlText(g_status, L"다운로드 준비 중...");
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);

    uintptr_t th = _beginthreadex(NULL, 0, DownloadThread, batch, 0, NULL);
    if (th) CloseHandle((HANDLE)th);
    else {
        free(batch);
        InterlockedExchange((LONG *)&g_download_running, 0);
    }
}

static HWND AddCtl(DWORD ex, const wchar_t *cls, const wchar_t *text, DWORD style,
                   int x, int y, int w, int h, int id, HWND parent) {
    HWND ctrl = CreateWindowExW(ex, cls, text, style, x, y, w, h, parent,
                                (HMENU)(INT_PTR)id, g_instance, NULL);
    ApplyClassic(ctrl);
    return ctrl;
}

static void InitListColumns(void) {
    struct Col { const wchar_t *name; int width; } cols[] = {
        { L"번호", 48 }, { L"제목", 250 }, { L"예상 이름", 290 },
        { L"예상 용량", 90 }, { L"상태", 80 }, { L"진행률", 70 }
    };
    for (int i = 0; i < 6; ++i) {
        LVCOLUMNW c;
        ZeroMemory(&c, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = (LPWSTR)cols[i].name;
        c.cx = cols[i].width;
        c.iSubItem = i;
        ListView_InsertColumn(g_list, i, &c);
    }
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
}

static void SetDefaultFolder(void) {
    wchar_t music[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYMUSIC | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, music))) {
        wchar_t folder[MAX_PATH];
        StringCchPrintfW(folder, MAX_PATH, L"%s\\YouTubeMP3", music);
        SHCreateDirectoryExW(g_main, folder, NULL);
        SetWindowTextW(g_folder_edit, folder);
    } else {
        SetWindowTextW(g_folder_edit, L"C:\\Music\\YouTubeMP3");
    }
    UpdateFolderStatsUI();
}

static void CreateUi(HWND hwnd) {
    g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Shell Dlg");

    HWND grp1 = AddCtl(0, L"BUTTON", L"1. 유튜브 링크 목록", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                       10, 10, 790, 210, 0, hwnd);
    (void)grp1;
    g_url_edit = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                        25, 35, 760, 110, IDC_URL_EDIT, hwnd);
    AddCtl(0, L"BUTTON", L"링크 추가", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           25, 155, 200, 42, IDC_ADD_LINKS, hwnd);
    AddCtl(0, L"BUTTON", L"파일 불러오기(.txt)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           245, 155, 250, 42, IDC_LOAD_TXT, hwnd);
    AddCtl(0, L"BUTTON", L"중복 제거", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           515, 155, 180, 42, IDC_REMOVE_DUP, hwnd);

    AddCtl(0, L"BUTTON", L"2. 다운로드 대기 목록", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           10, 225, 790, 335, 0, hwnd);
    g_list = AddCtl(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                    25, 250, 760, 295, IDC_LIST, hwnd);
    InitListColumns();

    AddCtl(0, L"BUTTON", L"3. 옵션", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           810, 10, 400, 260, 0, hwnd);
    g_chk_dedup = AddCtl(0, L"BUTTON", L"중복 링크 자동 제거", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                         830, 40, 340, 28, IDC_CHK_DEDUP, hwnd);
    g_chk_skip = AddCtl(0, L"BUTTON", L"이미 다운로드한 곡 건너뛰기", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                        830, 80, 350, 28, IDC_CHK_SKIP, hwnd);
    g_chk_sanitize = AddCtl(0, L"BUTTON", L"파일명 금지 문자 자동 제거", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                            830, 120, 350, 28, IDC_CHK_SANITIZE, hwnd);
    g_chk_clean = AddCtl(0, L"BUTTON", L"이름 자동 정리 (아티스트 - 곡명)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                         830, 160, 360, 28, IDC_CHK_CLEAN, hwnd);
    g_chk_size = AddCtl(0, L"BUTTON", L"다운로드 전 예상 파일 용량 표시", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                        830, 200, 360, 28, IDC_CHK_SIZE, hwnd);
    SendMessageW(g_chk_dedup, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_skip, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_sanitize, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_clean, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_size, BM_SETCHECK, BST_CHECKED, 0);

    AddCtl(0, L"BUTTON", L"4. 저장 폴더 정보", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           810, 280, 400, 280, 0, hwnd);
    g_folder_edit = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           830, 310, 265, 32, IDC_FOLDER_EDIT, hwnd);
    AddCtl(0, L"BUTTON", L"변경", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           1105, 308, 85, 36, IDC_FOLDER_BROWSE, hwnd);
    g_folder_stats = AddCtl(0, L"STATIC", L"현재 폴더: 0곡\r\n총 용량: -", WS_CHILD | WS_VISIBLE,
                            830, 370, 330, 65, IDC_FOLDER_STATS, hwnd);
    AddCtl(0, L"BUTTON", L"폴더 열기", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           1000, 485, 180, 48, IDC_FOLDER_OPEN, hwnd);

    AddCtl(0, L"BUTTON", L"전체 다운로드 시작", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
           20, 575, 300, 48, IDC_DOWNLOAD_ALL, hwnd);
    AddCtl(0, L"BUTTON", L"실패 항목만 재시도", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           340, 575, 280, 48, IDC_RETRY_FAILED, hwnd);
    AddCtl(0, L"BUTTON", L"선택 삭제", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           640, 575, 250, 48, IDC_DELETE_SELECTED, hwnd);
    AddCtl(0, L"BUTTON", L"종료", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
           910, 575, 280, 48, IDC_EXIT, hwnd);

    AddCtl(0, L"BUTTON", L"현재 작업", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           10, 635, 1200, 75, 0, hwnd);
    g_progress = AddCtl(WS_EX_CLIENTEDGE, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                        145, 655, 710, 24, IDC_PROGRESS, hwnd);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g_status = AddCtl(0, L"STATIC", L"상태: 대기 중", WS_CHILD | WS_VISIBLE,
                      25, 680, 830, 22, IDC_STATUS, hwnd);
    g_overall = AddCtl(0, L"STATIC", L"전체 진행률: 0 / 0", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                       870, 655, 315, 30, IDC_OVERALL, hwnd);

    AddCtl(0, L"BUTTON", L"파일명 미리보기", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
           10, 720, 1200, 90, 0, hwnd);
    AddCtl(0, L"STATIC", L"원본 제목:", WS_CHILD | WS_VISIBLE,
           25, 748, 110, 22, 0, hwnd);
    g_raw_title = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                         145, 742, 1040, 28, IDC_RAW_TITLE, hwnd);
    AddCtl(0, L"STATIC", L"정리된 이름:", WS_CHILD | WS_VISIBLE,
           25, 780, 110, 22, 0, hwnd);
    g_clean_title = AddCtl(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                           145, 774, 1040, 28, IDC_CLEAN_TITLE, hwnd);

    SetDefaultFolder();
}

static void UpdateOptionFromControl(int id) {
    HWND h = GetDlgItem(g_main, id);
    LONG value = (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    switch (id) {
        case IDC_CHK_DEDUP: InterlockedExchange((LONG *)&g_opt_dedup, value); break;
        case IDC_CHK_SKIP: InterlockedExchange((LONG *)&g_opt_skip, value); break;
        case IDC_CHK_SANITIZE: InterlockedExchange((LONG *)&g_opt_sanitize, value); RecomputeNames(); break;
        case IDC_CHK_CLEAN: InterlockedExchange((LONG *)&g_opt_clean, value); RecomputeNames(); break;
        case IDC_CHK_SIZE: InterlockedExchange((LONG *)&g_opt_size, value); RebuildList(); break;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_main = hwnd;
            CreateUi(hwnd);
            RefreshTools();
            if (!g_ytdlp[0]) SetControlText(g_status, L"상태: yt-dlp.exe 필요");
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case IDC_ADD_LINKS: AddUrlsFromEdit(); break;
                case IDC_LOAD_TXT: LoadTxtFile(); break;
                case IDC_REMOVE_DUP:
                    if (!g_meta_running && !g_download_running) {
                        CompactDuplicates();
                        RebuildList();
                    }
                    break;
                case IDC_FOLDER_BROWSE: BrowseFolder(); break;
                case IDC_FOLDER_OPEN: OpenFolder(); break;
                case IDC_DOWNLOAD_ALL: StartDownload(FALSE); break;
                case IDC_RETRY_FAILED: StartDownload(TRUE); break;
                case IDC_DELETE_SELECTED: DeleteSelected(); break;
                case IDC_EXIT: SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
                case IDC_CHK_DEDUP:
                case IDC_CHK_SKIP:
                case IDC_CHK_SANITIZE:
                case IDC_CHK_CLEAN:
                case IDC_CHK_SIZE:
                    UpdateOptionFromControl(id);
                    break;
            }
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR *hdr = (NMHDR *)lParam;
            if (hdr->idFrom == IDC_LIST && hdr->code == LVN_ITEMCHANGED) UpdatePreviewFromSelection();
            return 0;
        }

        case WM_APP_JOB_UPDATED:
            UpdateListRow((int)wParam);
            UpdatePreviewFromSelection();
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
            return 0;
        }

        case WM_APP_CURRENT_JOB: {
            int index = (int)wParam;
            Job j;
            BOOL found = FALSE;
            EnterCriticalSection(&g_jobs_lock);
            if (index >= 0 && index < g_job_count) {
                j = g_jobs[index];
                found = TRUE;
            }
            LeaveCriticalSection(&g_jobs_lock);
            if (found) {
                wchar_t text[1200];
                StringCchPrintfW(text, 1200, L"상태: %s 다운로드 중", j.clean_name[0] ? j.clean_name : j.raw_title);
                SetControlText(g_status, text);
            }
            return 0;
        }

        case WM_APP_CURRENT_PROGRESS:
            SendMessageW(g_progress, PBM_SETPOS, (int)wParam, 0);
            return 0;

        case WM_APP_OVERALL: {
            wchar_t text[128];
            StringCchPrintfW(text, 128, L"전체 진행률: %d / %d", (int)wParam, (int)lParam);
            SetControlText(g_overall, text);
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
            StringCchPrintfW(text, 180, L"상태: 작업 완료 (완료 %d, 건너뜀 %d, 실패 %d)",
                             done, skipped, failed);
            SendMessageW(g_progress, PBM_SETPOS, 100, 0);
            SetControlText(g_status, text);
            UpdateFolderStatsUI();
            return 0;
        }

        case WM_APP_FOLDER_STATS:
            UpdateFolderStatsUI();
            return 0;

        case WM_CLOSE:
            if (InterlockedCompareExchange((LONG *)&g_download_running, 0, 0) ||
                InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0)) {
                MessageBoxW(hwnd, L"현재 작업이 진행 중입니다. 작업이 끝난 뒤 종료해 주세요.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
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

static BOOL RunCoreSelfTests(void) {
    const wchar_t *const arguments[] = {
        L"tool.exe", L"plain", L"with space", L"C:\\trailing\\", L"quote\"inside", L""
    };
    wchar_t command[512];
    if (!BuildCommandLine(command, 512, arguments, sizeof(arguments) / sizeof(arguments[0]))) return FALSE;

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
    if (!EscapePowerShellSingleQuoted(L"C:\\O'Brien", escaped, 64) ||
        wcscmp(escaped, L"C:\\O''Brien")) return FALSE;

    wchar_t filename[128];
    StringCchCopyW(filename, 128, L"..\\bad:name?. ");
    SanitizeFilename(filename, 128);
    if (!IsSafeFilename(filename) || IsSafeFilename(L"..\\escape.mp3") ||
        IsSafeFilename(L"CON.mp3") || IsSafeFilename(L"CON .mp3") ||
        IsSafeFilename(L"song.mp3:stream")) return FALSE;

    if (!IsHttpUrl(L"HTTPS://www.youtube.com/watch?v=test") ||
        IsHttpUrl(L"ftp://example.com/file")) return FALSE;
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    g_instance = hInstance;
    SetProcessDPIAware();
    HRESULT co_init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitializeCriticalSection(&g_jobs_lock);
    int result = 1;
    GetAppDirectory(g_app_dir, MAX_PATH);

    if (lpCmdLine && wcsstr(lpCmdLine, L"--self-test-core")) {
        result = RunCoreSelfTests() ? 0 : 3;
        goto cleanup;
    }

    PrepareBundledTools();
    RefreshTools();
    if (lpCmdLine && wcsstr(lpCmdLine, L"--self-test-tools")) {
        wchar_t ffprobe[MAX_PATH];
        BOOL ok = g_ytdlp[0] && g_ffmpeg[0] && FindTool(L"ffprobe.exe", ffprobe, MAX_PATH);
        result = ok ? 0 : 2;
        goto cleanup;
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SeowolYTMP3ClassicWin32";
    wc.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
    if (!RegisterClassExW(&wc)) goto cleanup;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, APP_CLIENT_W, APP_CLIENT_H};
    AdjustWindowRect(&r, style, FALSE);
    int width = r.right - r.left;
    int height = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, style,
                                x, y, width, height, NULL, NULL, hInstance, NULL);
    if (!hwnd) goto cleanup;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

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
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

cleanup:
    DeleteCriticalSection(&g_jobs_lock);
    if (SUCCEEDED(co_init)) CoUninitialize();
    return result;
}
