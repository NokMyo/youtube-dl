from pathlib import Path

h = Path('src/account.h')
text = h.read_text(encoding='utf-8')
if 'AccountSyncResult' not in text:
    text += '''\n\ntypedef struct AccountSyncResult {\n    LONGLONG max_link_id;\n    wchar_t *links_text;\n} AccountSyncResult;\n\nBOOL Account_RequestSync(HWND target, UINT message);\nvoid Account_AcknowledgeSyncAsync(LONGLONG last_link_id);\nvoid Account_FreeSyncResult(AccountSyncResult *result);\n'''
    h.write_text(text, encoding='utf-8')

c = Path('src/account.c')
text = c.read_text(encoding='utf-8')
if '#include <process.h>' not in text:
    text = text.replace('#include <stdio.h>\n', '#include <stdio.h>\n#include <process.h>\n', 1)
if 'Account_RequestSync(HWND target' not in text:
    text += r'''

static volatile LONG g_account_sync_running = 0;

typedef struct AccountSyncWork { HWND target; UINT message; } AccountSyncWork;
typedef struct AccountAckWork { LONGLONG last_link_id; } AccountAckWork;

static BOOL ExtractJsonInt64(const char *json, const char *key, LONGLONG *value) {
    if (!json || !key || !value) return FALSE;
    char needle[128];
    if (FAILED(StringCchPrintfA(needle, sizeof(needle), "\"%s\":", key))) return FALSE;
    const char *p = strstr(json, needle);
    if (!p) return FALSE;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') ++p;
    char *end = NULL;
    long long parsed = _strtoi64(p, &end, 10);
    if (end == p) return FALSE;
    *value = (LONGLONG)parsed;
    return TRUE;
}

void Account_FreeSyncResult(AccountSyncResult *result) {
    if (!result) return;
    free(result->links_text);
    free(result);
}

static unsigned __stdcall AccountSyncWorker(void *param) {
    AccountSyncWork *work = (AccountSyncWork *)param;
    char token[ACCOUNT_TOKEN_MAX];
    if (!work || !LoadToken(token, sizeof(token))) {
        free(work);
        InterlockedExchange((LONG *)&g_account_sync_running, 0);
        return 0;
    }
    DWORD status = 0;
    char *response = (char *)calloc(ACCOUNT_RESPONSE_MAX, 1);
    BOOL ok = response && HttpRequest(L"GET", L"/api/app/sync", NULL, token,
                                      &status, response, ACCOUNT_RESPONSE_MAX);
    SecureZeroMemory(token, sizeof(token));
    if (!ok || status != 200) {
        if (status == 401 || status == 403) ClearToken();
        free(response); free(work);
        InterlockedExchange((LONG *)&g_account_sync_running, 0);
        return 0;
    }
    char *links_utf8 = (char *)calloc(ACCOUNT_RESPONSE_MAX, 1);
    LONGLONG max_id = 0;
    AccountSyncResult *result = NULL;
    if (links_utf8 && ExtractJsonString(response, "links_text", links_utf8, ACCOUNT_RESPONSE_MAX) &&
        ExtractJsonInt64(response, "max_id", &max_id)) {
        int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, links_utf8, -1, NULL, 0);
        if (chars > 0) {
            result = (AccountSyncResult *)calloc(1, sizeof(*result));
            if (result) {
                result->links_text = (wchar_t *)calloc((size_t)chars, sizeof(wchar_t));
                if (!result->links_text || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        links_utf8, -1, result->links_text, chars)) {
                    Account_FreeSyncResult(result); result = NULL;
                } else result->max_link_id = max_id;
            }
        }
    }
    if (links_utf8) { SecureZeroMemory(links_utf8, ACCOUNT_RESPONSE_MAX); free(links_utf8); }
    free(response);
    HWND target = work->target; UINT message = work->message; free(work);
    InterlockedExchange((LONG *)&g_account_sync_running, 0);
    if (result && (!IsWindow(target) || !PostMessageW(target, message, 0, (LPARAM)result)))
        Account_FreeSyncResult(result);
    return 0;
}

BOOL Account_RequestSync(HWND target, UINT message) {
    if (!target || !message || !Account_HasSavedSession()) return FALSE;
    if (InterlockedCompareExchange((LONG *)&g_account_sync_running, 1, 0) != 0) return FALSE;
    AccountSyncWork *work = (AccountSyncWork *)calloc(1, sizeof(*work));
    if (!work) { InterlockedExchange((LONG *)&g_account_sync_running, 0); return FALSE; }
    work->target = target; work->message = message;
    uintptr_t thread = _beginthreadex(NULL, 0, AccountSyncWorker, work, 0, NULL);
    if (!thread) { free(work); InterlockedExchange((LONG *)&g_account_sync_running, 0); return FALSE; }
    CloseHandle((HANDLE)thread);
    return TRUE;
}

static unsigned __stdcall AccountAckWorker(void *param) {
    AccountAckWork *work = (AccountAckWork *)param;
    char token[ACCOUNT_TOKEN_MAX];
    if (work && LoadToken(token, sizeof(token))) {
        char body[128]; DWORD status = 0; char response[2048];
        if (SUCCEEDED(StringCchPrintfA(body, sizeof(body), "{\"last_link_id\":%lld}",
                                      (long long)work->last_link_id))) {
            HttpRequest(L"POST", L"/api/app/sync", body, token, &status, response, sizeof(response));
            if (status == 401 || status == 403) ClearToken();
        }
        SecureZeroMemory(token, sizeof(token));
    }
    free(work); return 0;
}

void Account_AcknowledgeSyncAsync(LONGLONG last_link_id) {
    if (last_link_id <= 0 || !Account_HasSavedSession()) return;
    AccountAckWork *work = (AccountAckWork *)calloc(1, sizeof(*work));
    if (!work) return;
    work->last_link_id = last_link_id;
    uintptr_t thread = _beginthreadex(NULL, 0, AccountAckWorker, work, 0, NULL);
    if (thread) CloseHandle((HANDLE)thread); else free(work);
}
'''
    c.write_text(text, encoding='utf-8')

p = Path('src/main.c')
text = p.read_text(encoding='utf-8')
if '#define IDT_ACCOUNT_SYNC' not in text:
    text = text.replace('#define IDT_AUTO_UPDATE 3\n', '#define IDT_AUTO_UPDATE 3\n#define IDT_ACCOUNT_SYNC 4\n', 1)
if '#define WM_APP_ACCOUNT_SYNC' not in text:
    text = text.replace('#define WM_APP_UPDATE_READY     (WM_APP + 13)\n', '#define WM_APP_UPDATE_READY     (WM_APP + 13)\n#define WM_APP_ACCOUNT_SYNC     (WM_APP + 14)\n', 1)
marker = '''        case WM_APP_FOLDER_STATS:\n            ApplyFolderStatsResult((FolderStatsResult *)lParam);\n            return 0;\n'''
if 'case WM_APP_ACCOUNT_SYNC:' not in text:
    block = '''        case WM_APP_ACCOUNT_SYNC: {\n            AccountSyncResult *sync = (AccountSyncResult *)lParam;\n            if (!sync) return 0;\n            BOOL idle = !InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) &&\n                        !InterlockedCompareExchange((LONG *)&g_download_running, 0, 0) &&\n                        !InterlockedCompareExchange((LONG *)&g_tools_loading, 0, 0);\n            if (sync->links_text && sync->links_text[0] && idle && ToolsAvailable() &&\n                GetWindowTextLengthW(g_url_edit) == 0) {\n                SetWindowTextW(g_url_edit, sync->links_text);\n                AddUrlsFromEdit();\n                Account_AcknowledgeSyncAsync(sync->max_link_id);\n                SetControlText(g_status, L"상태: Febius 웹 보관 링크를 불러왔습니다.");\n            }\n            Account_FreeSyncResult(sync);\n            return 0;\n        }\n\n'''
    if marker not in text: raise SystemExit('WM_APP_FOLDER_STATS marker not found')
    text = text.replace(marker, block + marker, 1)
old = '''            if (ShouldCheckUpdatesAutomatically()) SetTimer(hwnd, IDT_AUTO_UPDATE, 1500, NULL);\n            CloseAfterWorkersIfRequested();\n'''
new = '''            if (ShouldCheckUpdatesAutomatically()) SetTimer(hwnd, IDT_AUTO_UPDATE, 1500, NULL);\n            SetTimer(hwnd, IDT_ACCOUNT_SYNC, 10000, NULL);\n            Account_RequestSync(hwnd, WM_APP_ACCOUNT_SYNC);\n            CloseAfterWorkersIfRequested();\n'''
if 'SetTimer(hwnd, IDT_ACCOUNT_SYNC' not in text:
    if old not in text: raise SystemExit('tools-ready marker not found')
    text = text.replace(old, new, 1)
old_timer = '''        case WM_TIMER:\n            if (wParam == IDT_AUTO_UPDATE) {\n                KillTimer(hwnd, IDT_AUTO_UPDATE);\n                StartUpdateCheck(TRUE);\n            }\n            return 0;\n'''
new_timer = '''        case WM_TIMER:\n            if (wParam == IDT_AUTO_UPDATE) {\n                KillTimer(hwnd, IDT_AUTO_UPDATE);\n                StartUpdateCheck(TRUE);\n            } else if (wParam == IDT_ACCOUNT_SYNC) {\n                BOOL idle = !InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) &&\n                            !InterlockedCompareExchange((LONG *)&g_download_running, 0, 0) &&\n                            !InterlockedCompareExchange((LONG *)&g_tools_loading, 0, 0);\n                if (idle && GetWindowTextLengthW(g_url_edit) == 0)\n                    Account_RequestSync(hwnd, WM_APP_ACCOUNT_SYNC);\n            }\n            return 0;\n'''
if 'wParam == IDT_ACCOUNT_SYNC' not in text:
    if old_timer not in text: raise SystemExit('WM_TIMER marker not found')
    text = text.replace(old_timer, new_timer, 1)
p.write_text(text, encoding='utf-8')
