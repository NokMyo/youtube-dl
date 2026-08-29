from pathlib import Path

main = Path('src/main.c')
text = main.read_text(encoding='utf-8')
text = text.replace('static HMENU g_options_menu;\nstatic HMENU g_quality_menu;', 'static HMENU g_options_menu;\nstatic HMENU g_quality_menu;\nstatic HMENU g_account_menu;')
old = '''    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGIN, L"Febius 계정 로그인...");
    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_STATUS, L"내 계정 정보");
    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_SYNC, L"저장된 링크 가져오기");
    AppendMenuW(account, MF_SEPARATOR, 0, NULL);
    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGOUT, L"로그아웃");'''
new = '''    g_account_menu = account;
    if (Account_HasSavedSession()) {
        AppendMenuW(account, MF_STRING, IDM_ACCOUNT_STATUS, L"내 계정 정보");
        AppendMenuW(account, MF_STRING, IDM_ACCOUNT_SYNC, L"저장된 링크 가져오기");
        AppendMenuW(account, MF_SEPARATOR, 0, NULL);
        AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGOUT, L"로그아웃");
    } else {
        AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGIN, L"Febius 계정 로그인...");
    }'''
if old not in text: raise SystemExit('account menu block not found')
text = text.replace(old, new, 1)
marker = 'static void CreateUi(HWND hwnd) {'
helper = '''static void RefreshAccountMenu(void) {
    if (!g_account_menu) return;
    while (GetMenuItemCount(g_account_menu) > 0) DeleteMenu(g_account_menu, 0, MF_BYPOSITION);
    if (Account_HasSavedSession()) {
        AppendMenuW(g_account_menu, MF_STRING, IDM_ACCOUNT_STATUS, L"내 계정 정보");
        AppendMenuW(g_account_menu, MF_STRING, IDM_ACCOUNT_SYNC, L"저장된 링크 가져오기");
        AppendMenuW(g_account_menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(g_account_menu, MF_STRING, IDM_ACCOUNT_LOGOUT, L"로그아웃");
    } else {
        AppendMenuW(g_account_menu, MF_STRING, IDM_ACCOUNT_LOGIN, L"Febius 계정 로그인...");
    }
    if (g_main) DrawMenuBar(g_main);
}

'''
if marker not in text: raise SystemExit('CreateUi marker not found')
text = text.replace(marker, helper + marker, 1)
text = text.replace('case IDM_ACCOUNT_LOGIN: Account_ShowLogin(hwnd); break;', 'case IDM_ACCOUNT_LOGIN: Account_ShowLogin(hwnd); RefreshAccountMenu(); break;')
text = text.replace('case IDM_ACCOUNT_STATUS: Account_ShowStatus(hwnd); break;', 'case IDM_ACCOUNT_STATUS: Account_ShowStatus(hwnd); RefreshAccountMenu(); break;')
text = text.replace('case IDM_ACCOUNT_LOGOUT: Account_Logout(hwnd); break;', 'case IDM_ACCOUNT_LOGOUT: Account_Logout(hwnd); RefreshAccountMenu(); break;')
main.write_text(text, encoding='utf-8')

acc = Path('src/account.c')
a = acc.read_text(encoding='utf-8')
start = a.index('void Account_ShowStatus(HWND owner) {')
end = a.index('\nvoid Account_Logout(HWND owner) {', start)
replacement = r'''typedef struct AccountInfoWindowState {
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
'''
a = a[:start] + replacement + a[end:]
acc.write_text(a, encoding='utf-8')
