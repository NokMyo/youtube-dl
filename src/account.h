#pragma once

#include <windows.h>

void Account_ShowLogin(HWND owner);
void Account_ShowStatus(HWND owner);
void Account_Logout(HWND owner);
BOOL Account_HasSavedSession(void);


typedef struct AccountSyncResult {
    BOOL ok;
    BOOL has_more;
    LONGLONG max_link_id;
    wchar_t *links_text;
    wchar_t error[256];
} AccountSyncResult;

BOOL Account_RequestSync(HWND target, UINT message);
void Account_AcknowledgeSyncAsync(LONGLONG last_link_id);
void Account_FreeSyncResult(AccountSyncResult *result);
