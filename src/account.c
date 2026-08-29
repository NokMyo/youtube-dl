#include "account.h"

#include <winhttp.h>
#include <wincrypt.h>
#include <strsafe.h>
#include <stdio.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")

#define ACCOUNT_API_HOST L"febius-auth.pages.dev"
#define ACCOUNT_REG_KEY L"Software\\NokMyo\\Febius\\Downrush"
#define ACCOUNT_TOKEN_VALUE L"AccountToken"
#define ACCOUNT_RESPONSE_MAX 65536
#define ACCOUNT_TOKEN_MAX 256
#define ACCOUNT_USERNAME_MAX 64

#define IDC_ACCOUNT_USERNAME 5101
#define IDC_ACCOUNT_PASSWORD 5102
#define IDC_ACCOUNT_LOGIN    5103
#define IDC_ACCOUNT_CANCEL   5104

typedef struct LoginWindowState {
    HWND owner;
    HWND username;
    HWND password;
    BOOL logged_in;
} LoginWindowState;

static BOOL Utf8FromWide(const wchar_t *value, char *out, size_t out_size) {
    if (!value || !out || out_size < 2) return FALSE;
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                    out, (int)out_size, NULL, NULL);
    return bytes > 0;
}

static BOOL WideFromUtf8(const char *value, wchar_t *out, size_t out_cch) {
    if (!value || !out || out_cch < 2) return FALSE;
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                                    out, (int)out_cch);
    return chars > 0;
}

static BOOL JsonEscapeWide(const wchar_t *value, char *out, size_t out_size) {
    char utf8[4096];
    if (!Utf8FromWide(value, utf8, sizeof(utf8))) return FALSE;
    size_t used = 0;
    for (size_t i = 0; utf8[i]; ++i) {
        unsigned char ch = (unsigned char)utf8[i];
        const char *escape = NULL;
        char unicode_escape[7];
        if (ch == '"') escape = "\\\"";
        else if (ch == '\\') escape = "\\\\";
        else if (ch == '\b') escape = "\\b";
        else if (ch == '\f') escape = "\\f";
        else if (ch == '\n') escape = "\\n";
        else if (ch == '\r') escape = "\\r";
        else if (ch == '\t') escape = "\\t";
        else if (ch < 0x20) {
            StringCchPrintfA(unicode_escape, 7, "\\u%04x", ch);
            escape = unicode_escape;
        }
        if (escape) {
            size_t len = strlen(escape);
            if (used + len + 1 > out_size) return FALSE;
            memcpy(out + used, escape, len);
            used += len;
        } else {
            if (used + 2 > out_size) return FALSE;
            out[used++] = (char)ch;
        }
    }
    out[used] = 0;
    SecureZeroMemory(utf8, sizeof(utf8));
    return TRUE;
}

static BOOL ExtractJsonString(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size < 2) return FALSE;
    char needle[128];
    if (FAILED(StringCchPrintfA(needle, sizeof(needle), "\"%s\":\"", key))) return FALSE;
    const char *p = strstr(json, needle);
    if (!p) return FALSE;
    p += strlen(needle);
    size_t used = 0;
    while (*p && *p != '"') {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\' && *p) {
            char esc = *p++;
            if (esc == 'n') ch = '\n';
            else if (esc == 'r') ch = '\r';
            else if (esc == 't') ch = '\t';
            else if (esc == 'b') ch = '\b';
            else if (esc == 'f') ch = '\f';
            else if (esc == '"' || esc == '\\' || esc == '/') ch = (unsigned char)esc;
            else return FALSE;
        }
        if (used + 2 > out_size) return FALSE;
        out[used++] = (char)ch;
    }
    if (*p != '"') return FALSE;
    out[used] = 0;
    return TRUE;
}

static BOOL SaveToken(const char *token) {
    if (!token || !*token) return FALSE;
    DATA_BLOB input, encrypted;
    input.pbData = (BYTE *)token;
    input.cbData = (DWORD)strlen(token) + 1;
    ZeroMemory(&encrypted, sizeof(encrypted));
    if (!CryptProtectData(&input, L"Febius Downrush account session", NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) return FALSE;

    HKEY key = NULL;
    BOOL success = FALSE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, ACCOUNT_REG_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        success = RegSetValueExW(key, ACCOUNT_TOKEN_VALUE, 0, REG_BINARY,
                                 encrypted.pbData, encrypted.cbData) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    SecureZeroMemory(encrypted.pbData, encrypted.cbData);
    LocalFree(encrypted.pbData);
    return success;
}

static BOOL LoadToken(char *out, size_t out_size) {
    if (!out || out_size < 2) return FALSE;
    out[0] = 0;
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, ACCOUNT_REG_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return FALSE;
    }
    DWORD type = 0, size = 0;
    LONG status = RegQueryValueExW(key, ACCOUNT_TOKEN_VALUE, NULL, &type, NULL, &size);
    if (status != ERROR_SUCCESS || type != REG_BINARY || size == 0 || size > 4096) {
        RegCloseKey(key);
        return FALSE;
    }
    BYTE *buffer = (BYTE *)malloc(size);
    if (!buffer) {
        RegCloseKey(key);
        return FALSE;
    }
    status = RegQueryValueExW(key, ACCOUNT_TOKEN_VALUE, NULL, &type, buffer, &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        SecureZeroMemory(buffer, size);
        free(buffer);
        return FALSE;
    }

    DATA_BLOB encrypted, plain;
    encrypted.pbData = buffer;
    encrypted.cbData = size;
    ZeroMemory(&plain, sizeof(plain));
    BOOL success = FALSE;
    if (CryptUnprotectData(&encrypted, NULL, NULL, NULL, NULL,
                           CRYPTPROTECT_UI_FORBIDDEN, &plain) && plain.cbData > 1) {
        size_t len = strnlen((const char *)plain.pbData, plain.cbData);
        if (len > 0 && len + 1 <= out_size) {
            memcpy(out, plain.pbData, len);
            out[len] = 0;
            success = TRUE;
        }
        SecureZeroMemory(plain.pbData, plain.cbData);
        LocalFree(plain.pbData);
    }
    SecureZeroMemory(buffer, size);
    free(buffer);
    return success;
}

static void ClearToken(void) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, ACCOUNT_REG_KEY, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, ACCOUNT_TOKEN_VALUE);
        RegCloseKey(key);
    }
}

BOOL Account_HasSavedSession(void) {
    char token[ACCOUNT_TOKEN_MAX];
    BOOL result = LoadToken(token, sizeof(token));
    SecureZeroMemory(token, sizeof(token));
    return result;
}

static BOOL HttpRequest(const wchar_t *method,
                        const wchar_t *path,
                        const char *body,
                        const char *token,
                        DWORD *status_code,
                        char *response,
                        size_t response_size) {
    if (!method || !path || !status_code || !response || response_size < 2) return FALSE;
    *status_code = 0;
    response[0] = 0;

    HINTERNET session = WinHttpOpen(L"Febius Downrush/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return FALSE;
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 12000);
    HINTERNET connect = WinHttpConnect(session, ACCOUNT_API_HOST,
                                       INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return FALSE;
    }
    HINTERNET request = WinHttpOpenRequest(connect, method, path, NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return FALSE;
    }

    wchar_t headers[1024] = L"Accept: application/json\r\n";
    if (body) StringCchCatW(headers, 1024, L"Content-Type: application/json; charset=utf-8\r\n");
    wchar_t auth[512];
    if (token && *token) {
        wchar_t token_wide[ACCOUNT_TOKEN_MAX];
        if (WideFromUtf8(token, token_wide, ACCOUNT_TOKEN_MAX) &&
            SUCCEEDED(StringCchPrintfW(auth, 512, L"Authorization: Bearer %s\r\n", token_wide))) {
            StringCchCatW(headers, 1024, auth);
        }
        SecureZeroMemory(token_wide, sizeof(token_wide));
    }

    DWORD body_size = body ? (DWORD)strlen(body) : 0;
    BOOL ok = WinHttpSendRequest(request, headers, (DWORD)-1L,
                                 body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
                                 body_size, body_size, 0) &&
              WinHttpReceiveResponse(request, NULL);
    if (ok) {
        DWORD status_size = sizeof(*status_code);
        ok = WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
    }

    size_t used = 0;
    while (ok && used + 1 < response_size) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            ok = FALSE;
            break;
        }
        if (!available) break;
        DWORD room = (DWORD)(response_size - used - 1);
        DWORD to_read = available < room ? available : room;
        DWORD read = 0;
        if (!WinHttpReadData(request, response + used, to_read, &read)) {
            ok = FALSE;
            break;
        }
        used += read;
        if (read < to_read) break;
        if (to_read < available) break;
    }
    response[used] = 0;

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

static void ShowApiError(HWND owner, DWORD status, const char *response) {
    char error_utf8[2048];
    wchar_t message[2048];
    if (ExtractJsonString(response, "error", error_utf8, sizeof(error_utf8)) &&
        WideFromUtf8(error_utf8, message, sizeof(message) / sizeof(message[0]))) {
        MessageBoxW(owner, message, L"Febius 계정", MB_OK | MB_ICONWARNING);
        return;
    }
    StringCchPrintfW(message, sizeof(message) / sizeof(message[0]),
                     L"계정 서버 요청에 실패했습니다. (HTTP %lu)", status);
    MessageBoxW(owner, message, L"Febius 계정", MB_OK | MB_ICONWARNING);
}

static BOOL PerformLogin(HWND owner, const wchar_t *username, const wchar_t *password) {
    char username_json[256], password_json[4096], body[8192];
    if (!JsonEscapeWide(username, username_json, sizeof(username_json)) ||
        !JsonEscapeWide(password, password_json, sizeof(password_json)) ||
        FAILED(StringCchPrintfA(body, sizeof(body),
            "{\"username\":\"%s\",\"password\":\"%s\",\"client_name\":\"Febius Downrush for Windows\"}",
            username_json, password_json))) {
        MessageBoxW(owner, L"로그인 정보를 처리하지 못했습니다.", L"Febius 계정", MB_OK | MB_ICONERROR);
        SecureZeroMemory(password_json, sizeof(password_json));
        SecureZeroMemory(body, sizeof(body));
        return FALSE;
    }

    DWORD status = 0;
    char response[ACCOUNT_RESPONSE_MAX];
    BOOL request_ok = HttpRequest(L"POST", L"/api/app/login", body, NULL,
                                  &status, response, sizeof(response));
    SecureZeroMemory(password_json, sizeof(password_json));
    SecureZeroMemory(body, sizeof(body));
    if (!request_ok) {
        MessageBoxW(owner, L"Febius 계정 서버에 연결할 수 없습니다.", L"Febius 계정", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    if (status != 200) {
        ShowApiError(owner, status, response);
        return FALSE;
    }

    char token[ACCOUNT_TOKEN_MAX];
    if (!ExtractJsonString(response, "token", token, sizeof(token)) || !SaveToken(token)) {
        SecureZeroMemory(token, sizeof(token));
        MessageBoxW(owner, L"로그인 세션을 안전하게 저장하지 못했습니다.", L"Febius 계정", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    SecureZeroMemory(token, sizeof(token));
    MessageBoxW(owner, L"Febius 계정에 로그인했습니다.\n라이선스가 확인되었습니다.",
                L"Febius 계정", MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

static LRESULT CALLBACK LoginWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LoginWindowState *state = (LoginWindowState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        state = (LoginWindowState *)create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
    }
    switch (msg) {
        case WM_CREATE: {
            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            CreateWindowW(L"STATIC", L"Febius Account", WS_CHILD | WS_VISIBLE,
                          20, 18, 280, 24, hwnd, NULL, NULL, NULL);
            CreateWindowW(L"STATIC", L"아이디", WS_CHILD | WS_VISIBLE,
                          20, 58, 80, 20, hwnd, NULL, NULL, NULL);
            state->username = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                          20, 78, 300, 26, hwnd, (HMENU)(INT_PTR)IDC_ACCOUNT_USERNAME, NULL, NULL);
            CreateWindowW(L"STATIC", L"비밀번호", WS_CHILD | WS_VISIBLE,
                          20, 116, 80, 20, hwnd, NULL, NULL, NULL);
            state->password = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                          20, 136, 300, 26, hwnd, (HMENU)(INT_PTR)IDC_ACCOUNT_PASSWORD, NULL, NULL);
            HWND login = CreateWindowW(L"BUTTON", L"로그인",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                          156, 184, 78, 28, hwnd, (HMENU)(INT_PTR)IDC_ACCOUNT_LOGIN, NULL, NULL);
            HWND cancel = CreateWindowW(L"BUTTON", L"취소",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                          242, 184, 78, 28, hwnd, (HMENU)(INT_PTR)IDC_ACCOUNT_CANCEL, NULL, NULL);
            SendMessageW(state->username, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(state->password, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(login, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(cancel, WM_SETFONT, (WPARAM)font, TRUE);
            SetFocus(state->username);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_ACCOUNT_LOGIN && state) {
                wchar_t username[ACCOUNT_USERNAME_MAX];
                wchar_t password[1025];
                GetWindowTextW(state->username, username, ACCOUNT_USERNAME_MAX);
                GetWindowTextW(state->password, password, 1025);
                if (wcslen(username) < 3 || wcslen(password) < 8) {
                    MessageBoxW(hwnd, L"아이디와 비밀번호를 확인해 주세요.", L"Febius 계정", MB_OK | MB_ICONWARNING);
                } else if (PerformLogin(hwnd, username, password)) {
                    state->logged_in = TRUE;
                    SecureZeroMemory(password, sizeof(password));
                    DestroyWindow(hwnd);
                    return 0;
                }
                SecureZeroMemory(password, sizeof(password));
                return 0;
            }
            if (LOWORD(wParam) == IDC_ACCOUNT_CANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Account_ShowLogin(HWND owner) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = LoginWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"FebiusDownrushAccountLogin";
        atom = RegisterClassW(&wc);
        if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(owner, L"로그인 창을 만들지 못했습니다.", L"Febius 계정", MB_OK | MB_ICONERROR);
            return;
        }
    }

    RECT owner_rect = { 0 };
    GetWindowRect(owner, &owner_rect);
    int width = 356, height = 268;
    int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    LoginWindowState state;
    ZeroMemory(&state, sizeof(state));
    state.owner = owner;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"FebiusDownrushAccountLogin",
                                  L"Febius 계정 로그인",
                                  WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height,
                                  owner, NULL, GetModuleHandleW(NULL), &state);
    if (!window) return;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG msg;
    while (IsWindow(window) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

typedef struct AccountInfoWindowState {
    wchar_t username[128];
} AccountInfoWindowState;

static LRESULT CALLBACK AccountInfoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AccountInfoWindowState *state = (AccountInfoWindowState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        state = (AccountInfoWindowState *)create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
    }
    switch (msg) {
        case WM_CREATE: {
            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HWND brand = CreateWindowW(L"STATIC", L"Febius", WS_CHILD | WS_VISIBLE,
                                       24, 20, 260, 30, hwnd, NULL, NULL, NULL);
            HWND section = CreateWindowW(L"STATIC", L"Account Information", WS_CHILD | WS_VISIBLE,
                                         24, 50, 260, 20, hwnd, NULL, NULL, NULL);
            CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                          24, 78, 352, 2, hwnd, NULL, NULL, NULL);
            const wchar_t *labels[] = { L"계정", L"제품", L"플랜", L"라이선스" };
            const wchar_t *values[] = { state ? state->username : L"-", L"Febius Downrush", L"Standard", L"확인됨" };
            for (int i = 0; i < 4; ++i) {
                HWND label = CreateWindowW(L"STATIC", labels[i], WS_CHILD | WS_VISIBLE,
                                           26, 102 + i * 36, 82, 22, hwnd, NULL, NULL, NULL);
                HWND value = CreateWindowW(L"STATIC", values[i], WS_CHILD | WS_VISIBLE,
                                           122, 102 + i * 36, 244, 22, hwnd, NULL, NULL, NULL);
                SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
                SendMessageW(value, WM_SETFONT, (WPARAM)font, TRUE);
            }
            HWND close = CreateWindowW(L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                       286, 258, 90, 30, hwnd, (HMENU)(INT_PTR)IDOK, NULL, NULL);
            SendMessageW(brand, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(section, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(close, WM_SETFONT, (WPARAM)font, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowAccountInfoWindow(HWND owner, const wchar_t *username) {
    static ATOM atom = 0;
    if (!atom) {
        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = AccountInfoWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"FebiusDownrushAccountInfo";
        atom = RegisterClassW(&wc);
        if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    }
    AccountInfoWindowState state;
    ZeroMemory(&state, sizeof(state));
    StringCchCopyW(state.username, 128, username && *username ? username : L"알 수 없음");
    RECT owner_rect = {0};
    GetWindowRect(owner, &owner_rect);
    int width = 420, height = 340;
    int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, L"FebiusDownrushAccountInfo",
                                  L"Febius 계정 정보", WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, owner, NULL, GetModuleHandleW(NULL), &state);
    if (!window) return;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG msg;
    while (IsWindow(window) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}

void Account_ShowStatus(HWND owner) {
    char token[ACCOUNT_TOKEN_MAX];
    if (!LoadToken(token, sizeof(token))) {
        MessageBoxW(owner, L"현재 로그인된 Febius 계정이 없습니다.", L"Febius 계정", MB_OK | MB_ICONINFORMATION);
        return;
    }

    DWORD status = 0;
    char response[ACCOUNT_RESPONSE_MAX];
    BOOL ok = HttpRequest(L"GET", L"/api/app/me", NULL, token,
                          &status, response, sizeof(response));
    SecureZeroMemory(token, sizeof(token));
    if (!ok) {
        MessageBoxW(owner, L"Febius 계정 서버에 연결할 수 없습니다.", L"Febius 계정", MB_OK | MB_ICONERROR);
        return;
    }
    if (status == 401 || status == 403) {
        ClearToken();
        ShowApiError(owner, status, response);
        return;
    }
    if (status != 200) {
        ShowApiError(owner, status, response);
        return;
    }

    char username_utf8[128];
    wchar_t username[128];
    if (!ExtractJsonString(response, "username", username_utf8, sizeof(username_utf8)) ||
        !WideFromUtf8(username_utf8, username, sizeof(username) / sizeof(username[0]))) {
        StringCchCopyW(username, 128, L"알 수 없음");
    }
    ShowAccountInfoWindow(owner, username);
}

void Account_Logout(HWND owner) {
    char token[ACCOUNT_TOKEN_MAX];
    if (LoadToken(token, sizeof(token))) {
        DWORD status = 0;
        char response[2048];
        HttpRequest(L"POST", L"/api/app/logout", "{}", token,
                    &status, response, sizeof(response));
        SecureZeroMemory(token, sizeof(token));
    }
    ClearToken();
    MessageBoxW(owner, L"Febius 계정에서 로그아웃했습니다.", L"Febius 계정", MB_OK | MB_ICONINFORMATION);
}


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
