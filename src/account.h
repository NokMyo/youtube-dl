#pragma once

#include <windows.h>

void Account_ShowLogin(HWND owner);
void Account_ShowStatus(HWND owner);
void Account_Logout(HWND owner);
BOOL Account_HasSavedSession(void);
