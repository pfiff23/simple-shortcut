#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include "keyboard_hook.h"
#include "settings.h"
#include "resource.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define MUTEX_NAME     L"SimpleShortcut_SingleInstance"
#define WND_CLASS_NAME L"SimpleShortcutTrayClass"
#define WM_TRAYICON    (WM_APP + 1)
#define AUTOSTART_KEY  L"SimpleShortcut"
#define REG_RUN_PATH   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

static HMENU          g_hMenu;
static NOTIFYICONDATA g_nid;
static HINSTANCE      g_hInst;

static bool Autostart_IsEnabled()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD cbData = MAX_PATH * sizeof(WCHAR);
    WCHAR szValue[MAX_PATH];
    LSTATUS status = RegQueryValueExW(hKey, AUTOSTART_KEY, NULL, NULL, (LPBYTE)szValue, &cbData);
    RegCloseKey(hKey);
    return (status == ERROR_SUCCESS);
}

static void Autostart_Set(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (enable)
    {
        WCHAR szExePath[MAX_PATH];
        DWORD n = GetModuleFileNameW(NULL, szExePath, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) { RegCloseKey(hKey); return; }
        WCHAR szQuoted[MAX_PATH + 3];
        _snwprintf_s(szQuoted, _TRUNCATE, L"\"%s\"", szExePath);
        RegSetValueExW(hKey, AUTOSTART_KEY, 0, REG_SZ, (const BYTE*)szQuoted,
                       (DWORD)((wcslen(szQuoted) + 1) * sizeof(WCHAR)));
    }
    else
    {
        RegDeleteValueW(hKey, AUTOSTART_KEY);
    }

    RegCloseKey(hKey);
}

static void BuildMenu()
{
    g_hMenu = CreatePopupMenu();

    MENUITEMINFOW mii = { sizeof(MENUITEMINFOW) };

    mii.fMask      = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE;
    mii.fType      = MFT_STRING;
    mii.fState     = MFS_DISABLED;
    mii.wID        = IDM_TRAY_HEADER;
    mii.dwTypeData = (LPWSTR)L"Simple Shortcut";
    InsertMenuItemW(g_hMenu, 0, TRUE, &mii);

    mii.fMask = MIIM_FTYPE;
    mii.fType = MFT_SEPARATOR;
    InsertMenuItemW(g_hMenu, 1, TRUE, &mii);

    mii.fMask      = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE;
    mii.fType      = MFT_STRING;
    mii.fState     = MFS_ENABLED;
    mii.wID        = IDM_OPEN_SETTINGS;
    mii.dwTypeData = (LPWSTR)L"Open Settings";
    InsertMenuItemW(g_hMenu, 2, TRUE, &mii);

    bool aStart = Autostart_IsEnabled();
    mii.fMask      = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE;
    mii.fType      = MFT_STRING;
    mii.fState     = MFS_ENABLED | (aStart ? MFS_CHECKED : MFS_UNCHECKED);
    mii.wID        = IDM_AUTOSTART;
    mii.dwTypeData = (LPWSTR)L"Autostart";
    InsertMenuItemW(g_hMenu, 3, TRUE, &mii);

    mii.fMask = MIIM_FTYPE;
    mii.fType = MFT_SEPARATOR;
    InsertMenuItemW(g_hMenu, 4, TRUE, &mii);

    mii.fMask      = MIIM_FTYPE | MIIM_STRING | MIIM_ID | MIIM_STATE;
    mii.fType      = MFT_STRING;
    mii.fState     = MFS_ENABLED;
    mii.wID        = IDM_EXIT;
    mii.dwTypeData = (LPWSTR)L"Exit";
    InsertMenuItemW(g_hMenu, 5, TRUE, &mii);
}

static void Tray_Init(HWND hWnd, HINSTANCE hInstance)
{
    g_hInst = hInstance;

    g_nid.cbSize           = sizeof(NOTIFYICONDATA);
    g_nid.hWnd             = hWnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    wcscpy_s(g_nid.szTip, L"Simple Shortcut");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    BuildMenu();
}

static void Tray_ShowContextMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hWnd);

    TrackPopupMenu(g_hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hWnd, NULL);

    PostMessageW(hWnd, WM_NULL, 0, 0);
}

static void Tray_OnCommand(HWND hWnd, WPARAM wParam)
{
    switch (LOWORD(wParam))
    {
    case IDM_OPEN_SETTINGS:
        Settings_Create(g_hInst, hWnd);
        break;

    case IDM_AUTOSTART:
    {
        bool next = !Autostart_IsEnabled();
        CheckMenuItem(g_hMenu, IDM_AUTOSTART, next ? MF_CHECKED : MF_UNCHECKED);
        Autostart_Set(next);
    }
    break;

    case IDM_EXIT:
        DestroyWindow(hWnd);
        break;
    }
}

static void Tray_Cleanup()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    DestroyMenu(g_hMenu);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
            Tray_ShowContextMenu(hWnd);
        else if (lParam == WM_LBUTTONDBLCLK)
            Tray_OnCommand(hWnd, IDM_OPEN_SETTINGS);
        break;

    case WM_COMMAND:
        Tray_OnCommand(hWnd, wParam);
        break;

    case WM_DESTROY:
        Tray_Cleanup();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(NULL, L"Simple Shortcut is already running.",
                    L"Simple Shortcut", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WND_CLASS_NAME;
    RegisterClassW(&wc);

    Settings_RegisterClass(hInstance);

    HWND hWnd = CreateWindowExW(0, WND_CLASS_NAME, L"", 0,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                NULL, NULL, hInstance, NULL);

    Tray_Init(hWnd, hInstance);
    KeyboardHook_Install();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KeyboardHook_Uninstall();
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
