from pathlib import Path

p = Path('src/main.c')
s = p.read_text(encoding='utf-8')

repls = [
    ('#define IDT_AUTO_UPDATE 3\n#define IDT_ACCOUNT_SYNC 4\n', '#define IDT_AUTO_UPDATE 3\n'),
    ('#define IDM_ACCOUNT_LOGOUT      2062\n', '#define IDM_ACCOUNT_LOGOUT      2062\n#define IDM_ACCOUNT_SYNC        2063\n'),
    ('    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGIN, L"Febius 계정 로그인...");\n    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_STATUS, L"내 계정 정보");\n    AppendMenuW(account, MF_SEPARATOR, 0, NULL);\n    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGOUT, L"로그아웃");',
     '    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGIN, L"Febius 계정 로그인...");\n    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_STATUS, L"내 계정 정보");\n    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_SYNC, L"저장된 링크 가져오기");\n    AppendMenuW(account, MF_SEPARATOR, 0, NULL);\n    AppendMenuW(account, MF_STRING, IDM_ACCOUNT_LOGOUT, L"로그아웃");'),
    ('case IDM_ACCOUNT_LOGIN: Account_ShowLogin(hwnd); break;\ncase IDM_ACCOUNT_STATUS: Account_ShowStatus(hwnd); break;\ncase IDM_ACCOUNT_LOGOUT: Account_Logout(hwnd); break;',
     'case IDM_ACCOUNT_LOGIN: Account_ShowLogin(hwnd); break;\ncase IDM_ACCOUNT_STATUS: Account_ShowStatus(hwnd); break;\ncase IDM_ACCOUNT_SYNC:\n    if (!Account_HasSavedSession()) {\n        MessageBoxW(hwnd, L"Febius 계정에 먼저 로그인해 주세요.", L"저장된 링크 가져오기", MB_OK | MB_ICONINFORMATION);\n    } else if (g_meta_running || g_download_running || g_tools_loading) {\n        MessageBoxW(hwnd, L"현재 작업이 끝난 뒤 저장된 링크를 가져와 주세요.", L"저장된 링크 가져오기", MB_OK | MB_ICONINFORMATION);\n    } else if (GetWindowTextLengthW(g_url_edit) != 0) {\n        MessageBoxW(hwnd, L"링크 입력칸을 비운 뒤 다시 시도해 주세요.", L"저장된 링크 가져오기", MB_OK | MB_ICONINFORMATION);\n    } else if (Account_RequestSync(hwnd, WM_APP_ACCOUNT_SYNC)) {\n        SetControlText(g_status, L"상태: Febius 웹의 저장된 링크 확인 중...");\n    } else {\n        MessageBoxW(hwnd, L"링크 가져오기를 시작하지 못했습니다. 잠시 후 다시 시도해 주세요.", L"저장된 링크 가져오기", MB_OK | MB_ICONWARNING);\n    }\n    break;\ncase IDM_ACCOUNT_LOGOUT: Account_Logout(hwnd); break;'),
    ('            if (sync->links_text && sync->links_text[0] && idle && ToolsAvailable() &&\n                GetWindowTextLengthW(g_url_edit) == 0) {\n                SetWindowTextW(g_url_edit, sync->links_text);\n                AddUrlsFromEdit();\n                Account_AcknowledgeSyncAsync(sync->max_link_id);\n                SetControlText(g_status, L"상태: Febius 웹 보관 링크를 불러왔습니다.");\n            }\n            Account_FreeSyncResult(sync);',
     '            if (sync->links_text && sync->links_text[0] && idle && ToolsAvailable() &&\n                GetWindowTextLengthW(g_url_edit) == 0) {\n                SetWindowTextW(g_url_edit, sync->links_text);\n                AddUrlsFromEdit();\n                Account_AcknowledgeSyncAsync(sync->max_link_id);\n                SetControlText(g_status, L"상태: 저장된 링크를 불러왔습니다.");\n            } else if (!sync->links_text || !sync->links_text[0]) {\n                SetControlText(g_status, L"상태: 새로 가져올 저장된 링크가 없습니다.");\n                MessageBoxW(hwnd, L"새로 가져올 저장된 링크가 없습니다.", L"저장된 링크 가져오기", MB_OK | MB_ICONINFORMATION);\n            } else {\n                SetControlText(g_status, L"상태: 지금은 저장된 링크를 가져올 수 없습니다.");\n            }\n            Account_FreeSyncResult(sync);'),
    ('            if (ShouldCheckUpdatesAutomatically()) SetTimer(hwnd, IDT_AUTO_UPDATE, 1500, NULL);\n            SetTimer(hwnd, IDT_ACCOUNT_SYNC, 10000, NULL);\n            Account_RequestSync(hwnd, WM_APP_ACCOUNT_SYNC);\n            CloseAfterWorkersIfRequested();',
     '            if (ShouldCheckUpdatesAutomatically()) SetTimer(hwnd, IDT_AUTO_UPDATE, 1500, NULL);\n            CloseAfterWorkersIfRequested();'),
    ('            } else if (wParam == IDT_ACCOUNT_SYNC) {\n                BOOL idle = !InterlockedCompareExchange((LONG *)&g_meta_running, 0, 0) &&\n                            !InterlockedCompareExchange((LONG *)&g_download_running, 0, 0) &&\n                            !InterlockedCompareExchange((LONG *)&g_tools_loading, 0, 0);\n                if (idle && GetWindowTextLengthW(g_url_edit) == 0)\n                    Account_RequestSync(hwnd, WM_APP_ACCOUNT_SYNC);\n            }\n', '            }\n')
]

for old, new in repls:
    if old not in s:
        raise SystemExit('expected snippet not found:\n' + old[:160])
    s = s.replace(old, new, 1)

p.write_text(s, encoding='utf-8')
