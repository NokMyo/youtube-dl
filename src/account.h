#pragma once

#include <windows.h>

void Account_ShowLogin(HWND owner);
void Account_ShowStatus(HWND owner);
void Account_Logout(HWND owner);
BOOL Account_HasSavedSession(void);


typedef struct AccountSyncResult {
    LONGLONG max_link_id;
    wchar_t *links_text;
} AccountSyncResult;

BOOL Account_RequestSync(HWND target, UINT message);
void Account_AcknowledgeSyncAsync(LONGLONG last_link_id);
void Account_FreeSyncResult(AccountSyncResult *result);
