#include "keyboard_hook.h"
#include "shortcuts.h"
#include <windows.h>
#include <vector>

static HHOOK    g_hHook       = NULL;
static DWORD    g_eatVk       = 0;
static bool     g_suppressUp  = false;

struct ParsedHotkey
{
    DWORD modifiers;
    DWORD vk;
    int   textIdx;
};

static std::vector<ParsedHotkey> g_hotkeys;
static std::vector<wchar_t*>     g_texts;
static std::vector<wchar_t*>     g_shortcuts;
static std::vector<int>          g_shortcutLens;

static wchar_t g_typeBuf[512];
static int     g_typeLen = 0;

static DWORD NameToVk(const wchar_t* name)
{
    if (!name || !name[0]) return 0;

    if (wcscmp(name, L"SPACE") == 0)      return VK_SPACE;
    if (wcscmp(name, L"ENTER") == 0)      return VK_RETURN;
    if (wcscmp(name, L"TAB") == 0)        return VK_TAB;
    if (wcscmp(name, L"ESCAPE") == 0)     return VK_ESCAPE;
    if (wcscmp(name, L"BACKSPACE") == 0)  return VK_BACK;
    if (wcscmp(name, L"DELETE") == 0)     return VK_DELETE;
    if (wcscmp(name, L"INSERT") == 0)     return VK_INSERT;
    if (wcscmp(name, L"HOME") == 0)       return VK_HOME;
    if (wcscmp(name, L"END") == 0)        return VK_END;
    if (wcscmp(name, L"PGUP") == 0)       return VK_PRIOR;
    if (wcscmp(name, L"PGDN") == 0)       return VK_NEXT;
    if (wcscmp(name, L"PRINT") == 0)      return VK_SNAPSHOT;
    if (wcscmp(name, L"SNAPSHOT") == 0)   return VK_SNAPSHOT;
    if (wcscmp(name, L"SCROLL") == 0)     return VK_SCROLL;
    if (wcscmp(name, L"PAUSE") == 0)      return VK_PAUSE;
    if (wcscmp(name, L"CAPSLOCK") == 0)   return VK_CAPITAL;
    if (wcscmp(name, L"NUMLOCK") == 0)    return VK_NUMLOCK;
    if (wcscmp(name, L"MULTIPLY") == 0)   return VK_MULTIPLY;
    if (wcscmp(name, L"ADD") == 0)        return VK_ADD;
    if (wcscmp(name, L"SUBTRACT") == 0)   return VK_SUBTRACT;
    if (wcscmp(name, L"DECIMAL") == 0)    return VK_DECIMAL;
    if (wcscmp(name, L"DIVIDE") == 0)     return VK_DIVIDE;
    if (wcscmp(name, L"SEPARATOR") == 0)  return VK_SEPARATOR;
    if (wcscmp(name, L"LEFT") == 0)       return VK_LEFT;
    if (wcscmp(name, L"RIGHT") == 0)      return VK_RIGHT;
    if (wcscmp(name, L"UP") == 0)         return VK_UP;
    if (wcscmp(name, L"DOWN") == 0)       return VK_DOWN;
    if (wcscmp(name, L"MINUS") == 0)      return VK_OEM_MINUS;
    if (wcscmp(name, L"PLUS") == 0)       return VK_OEM_PLUS;
    if (wcscmp(name, L"PERIOD") == 0)     return VK_OEM_PERIOD;
    if (wcscmp(name, L"COMMA") == 0)      return VK_OEM_COMMA;
    if (wcscmp(name, L"SEMICOLON") == 0)  return VK_OEM_1;
    if (wcscmp(name, L"SLASH") == 0)      return VK_OEM_2;
    if (wcscmp(name, L"TILDE") == 0)      return VK_OEM_3;
    if (wcscmp(name, L"LBRACKET") == 0)   return VK_OEM_4;
    if (wcscmp(name, L"BACKSLASH") == 0)  return VK_OEM_5;
    if (wcscmp(name, L"RBRACKET") == 0)   return VK_OEM_6;
    if (wcscmp(name, L"QUOTE") == 0)      return VK_OEM_7;
    if (wcscmp(name, L"OEM_8") == 0)      return VK_OEM_8;
    if (wcscmp(name, L"OEM_102") == 0 || wcscmp(name, L"ANGLE") == 0) return VK_OEM_102;

    if (name[0] >= L'A' && name[0] <= L'Z' && name[1] == 0) return name[0];
    if (name[0] >= L'0' && name[0] <= L'9' && name[1] == 0) return name[0];

    if (name[0] == L'F')
    {
        int num = _wtoi(name + 1);
        if (num >= 1 && num <= 24) return VK_F1 + (DWORD)(num - 1);
    }
    if (wcsncmp(name, L"NUM", 3) == 0)
    {
        int num = _wtoi(name + 3);
        if (num >= 0 && num <= 9) return VK_NUMPAD0 + (DWORD)num;
    }
    if (wcsncmp(name, L"KEY_", 4) == 0)
        return (DWORD)_wtoi(name + 4);

    return 0;
}

static DWORD ParseBind(const wchar_t* bind, DWORD* outMods)
{
    *outMods = 0;
    if (!bind || !bind[0]) return 0;

    wchar_t buf[128];
    wcscpy_s(buf, bind);
    const wchar_t* keyPart = buf;
    wchar_t* ctx = NULL;
    wchar_t* token = wcstok_s(buf, L"+", &ctx);
    while (token)
    {
        if (_wcsicmp(token, L"CONTROL") == 0 || _wcsicmp(token, L"CTRL") == 0)
            *outMods |= 1;
        else if (_wcsicmp(token, L"SHIFT") == 0)
            *outMods |= 2;
        else if (_wcsicmp(token, L"ALT") == 0)
            *outMods |= 4;
        else
            keyPart = token;
        token = wcstok_s(NULL, L"+", &ctx);
    }
    return NameToVk(keyPart);
}

static DWORD ModsFromState()
{
    DWORD m = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= 1;
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) m |= 2;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) m |= 4;
    return m;
}

static INPUT g_sendBuf[768];

static int AddKey(int idx, WORD vk, DWORD flags)
{
    INPUT* in = &g_sendBuf[idx];
    in->type           = INPUT_KEYBOARD;
    in->ki.wVk         = vk;
    in->ki.wScan       = 0;
    in->ki.dwFlags     = flags;
    in->ki.time        = 0;
    in->ki.dwExtraInfo = 0;
    return idx + 1;
}

static int AddChar(int idx, wchar_t ch, DWORD flags)
{
    INPUT* in = &g_sendBuf[idx];
    in->type           = INPUT_KEYBOARD;
    in->ki.wVk         = 0;
    in->ki.wScan       = ch;
    in->ki.dwFlags     = KEYEVENTF_UNICODE | flags;
    in->ki.time        = 0;
    in->ki.dwExtraInfo = 0;
    return idx + 1;
}

struct ClipRestoreData
{
    wchar_t* oldText;
};
static DWORD WINAPI ClipRestoreThread(LPVOID param)
{
    Sleep(100);
    ClipRestoreData* d = (ClipRestoreData*)param;
    if (d->oldText && OpenClipboard(NULL))
    {
        EmptyClipboard();
        int cb = ((int)wcslen(d->oldText) + 1) * (int)sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cb);
        if (hMem)
        {
            void* dst = GlobalLock(hMem);
            if (dst)
            {
                memcpy(dst, d->oldText, cb);
                GlobalUnlock(hMem);
                if (!SetClipboardData(CF_UNICODETEXT, hMem))
                    GlobalFree(hMem);
            }
            else
                GlobalFree(hMem);
        }
        CloseClipboard();
    }
    free(d->oldText);
    delete d;
    return 0;
}

static void SendReplacement(int backspaces, const wchar_t* text)
{
    int len = (int)wcslen(text);
    if (len == 0 && backspaces <= 0) return;

    int idx = 0;
    for (int i = 0; i < backspaces; i++)
    {
        idx = AddKey(idx, VK_BACK, 0);
        idx = AddKey(idx, VK_BACK, KEYEVENTF_KEYUP);
    }

    if (len > 100)
    {
        wchar_t* oldText = NULL;
        if (OpenClipboard(NULL))
        {
            HANDLE hOld = GetClipboardData(CF_UNICODETEXT);
            if (hOld)
            {
                wchar_t* src = (wchar_t*)GlobalLock(hOld);
                if (src) { oldText = _wcsdup(src); GlobalUnlock(hOld); }
            }
            EmptyClipboard();
            int cb = (len + 1) * (int)sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, cb);
            if (hMem)
            {
                void* dst = GlobalLock(hMem);
                if (dst)
                {
                    memcpy(dst, text, cb);
                    GlobalUnlock(hMem);
                    if (!SetClipboardData(CF_UNICODETEXT, hMem))
                        GlobalFree(hMem);
                }
                else
                    GlobalFree(hMem);
            }
            CloseClipboard();
        }

        idx = AddKey(idx, VK_CONTROL, 0);
        idx = AddKey(idx, 'V', 0);
        idx = AddKey(idx, 'V', KEYEVENTF_KEYUP);
        idx = AddKey(idx, VK_CONTROL, KEYEVENTF_KEYUP);
        SendInput((UINT)idx, g_sendBuf, sizeof(INPUT));

        if (oldText)
        {
            ClipRestoreData* rd = new ClipRestoreData;
            rd->oldText = oldText;
            HANDLE hThread = CreateThread(NULL, 0, ClipRestoreThread, rd, 0, NULL);
            if (hThread)
                CloseHandle(hThread);
            else
            {
                free(oldText);
                delete rd;
            }
        }
        return;
    }

    bool shiftDown = false;
    for (int i = 0; i < len; i++)
    {
        if (text[i] == L'\r' && i + 1 < len && text[i + 1] == L'\n')
        {
            i++;
            if (!shiftDown) { idx = AddKey(idx, VK_SHIFT, 0); shiftDown = true; }
            idx = AddKey(idx, VK_RETURN, 0);
            idx = AddKey(idx, VK_RETURN, KEYEVENTF_KEYUP);
        }
        else
        {
            if (shiftDown) { idx = AddKey(idx, VK_SHIFT, KEYEVENTF_KEYUP); shiftDown = false; }
            idx = AddChar(idx, text[i], 0);
            idx = AddChar(idx, text[i], KEYEVENTF_KEYUP);
        }
    }
    if (shiftDown)
        idx = AddKey(idx, VK_SHIFT, KEYEVENTF_KEYUP);

    SendInput((UINT)idx, g_sendBuf, sizeof(INPUT));
}

static void ReleaseModifiers()
{
    INPUT up[3] = {};
    int n = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) { up[n].type = INPUT_KEYBOARD; up[n].ki.wVk = VK_CONTROL; up[n].ki.dwFlags = KEYEVENTF_KEYUP; n++; }
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) { up[n].type = INPUT_KEYBOARD; up[n].ki.wVk = VK_MENU;    up[n].ki.dwFlags = KEYEVENTF_KEYUP; n++; }
    if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) { up[n].type = INPUT_KEYBOARD; up[n].ki.wVk = VK_SHIFT;   up[n].ki.dwFlags = KEYEVENTF_KEYUP; n++; }
    if (n > 0) SendInput(n, up, sizeof(INPUT));
}

static void PushTypedChar(wchar_t ch)
{
    if (g_typeLen < 510)
        g_typeBuf[g_typeLen++] = ch;
    g_typeBuf[g_typeLen] = 0;
}

static bool TryExpand()
{
    for (size_t i = 0; i < g_shortcuts.size(); i++)
    {
        int sLen = g_shortcutLens[i];
        if (sLen == 0 || g_typeLen < sLen) continue;
        if (_wcsicmp(g_typeBuf + g_typeLen - sLen, g_shortcuts[i]) == 0)
        {
            SendReplacement(sLen - 1, g_texts[i]);
            g_typeLen = 0;
            g_typeBuf[0] = 0;
            return true;
        }
    }
    return false;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION)
        return CallNextHookEx(g_hHook, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

    if (p->flags & LLKHF_INJECTED)
        return CallNextHookEx(g_hHook, nCode, wParam, lParam);

    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
    {
        if (g_suppressUp && p->vkCode == g_eatVk)
        {
            g_suppressUp = false;
            g_eatVk = 0;
            return 1;
        }
        return CallNextHookEx(g_hHook, nCode, wParam, lParam);
    }

    if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN)
        return CallNextHookEx(g_hHook, nCode, wParam, lParam);

    DWORD vk = p->vkCode;

    if (!(vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN))
    {
        DWORD mods = ModsFromState();

        for (size_t i = 0; i < g_hotkeys.size(); i++)
        {
            if (g_hotkeys[i].vk == vk && g_hotkeys[i].modifiers == mods)
            {
                g_eatVk = vk;
                g_suppressUp = true;
                ReleaseModifiers();
                SendReplacement(0, g_texts[g_hotkeys[i].textIdx]);
                g_typeLen = 0;
                return 1;
            }
        }
    }

    if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000))
    {
        if (vk >= 'A' && vk <= 'Z')
        {
            bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            PushTypedChar(shift ? (wchar_t)vk : (wchar_t)(vk + 32));
            if (TryExpand())
                return 1;
        }
        else if (vk >= '0' && vk <= '9')
        {
            PushTypedChar((wchar_t)vk);
            if (TryExpand())
                return 1;
        }
        else if (vk == VK_BACK)
        {
            if (g_typeLen > 0)
                g_typeBuf[--g_typeLen] = 0;
        }
        else if (vk == VK_SPACE)
        {
            PushTypedChar(L' ');
        }
        else if (vk == VK_RETURN || vk == VK_TAB || vk == VK_ESCAPE ||
                 vk == VK_OEM_1 || vk == VK_OEM_2 || vk == VK_OEM_3 ||
                 vk == VK_OEM_4 || vk == VK_OEM_5 || vk == VK_OEM_6 ||
                 vk == VK_OEM_7 || vk == VK_OEM_PERIOD || vk == VK_OEM_COMMA ||
                 vk == VK_OEM_MINUS || vk == VK_OEM_PLUS)
        {
            g_typeLen = 0;
        }
    }

    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

void KeyboardHook_Install()
{
    g_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                GetModuleHandleW(NULL), 0);
    KeyboardHook_ReloadShortcuts();
}

void KeyboardHook_Uninstall()
{
    if (g_hHook) { UnhookWindowsHookEx(g_hHook); g_hHook = NULL; }

    for (size_t i = 0; i < g_texts.size(); i++)     free(g_texts[i]);
    for (size_t i = 0; i < g_shortcuts.size(); i++)  { if (g_shortcuts[i]) free(g_shortcuts[i]); }
    g_texts.clear();
    g_shortcuts.clear();
    g_shortcutLens.clear();
    g_hotkeys.clear();
}

void KeyboardHook_ReloadShortcuts()
{
    for (size_t i = 0; i < g_texts.size(); i++)     free(g_texts[i]);
    for (size_t i = 0; i < g_shortcuts.size(); i++)  { if (g_shortcuts[i]) free(g_shortcuts[i]); }
    g_texts.clear();
    g_shortcuts.clear();
    g_shortcutLens.clear();
    g_hotkeys.clear();

    std::vector<ShortcutEntry> entries;
    if (!Shortcuts_Load(entries)) return;

    for (size_t i = 0; i < entries.size(); i++)
    {
        if (!entries[i].text[0]) continue;
        if (!entries[i].shortcut[0] && !entries[i].bind[0]) continue;

        wchar_t* t = _wcsdup(entries[i].text);
        g_texts.push_back(t);

        if (entries[i].shortcut[0])
        {
            g_shortcuts.push_back(_wcsdup(entries[i].shortcut));
            g_shortcutLens.push_back((int)wcslen(entries[i].shortcut));
        }
        else
        {
            g_shortcuts.push_back(NULL);
            g_shortcutLens.push_back(0);
        }

        if (entries[i].bind[0])
        {
            ParsedHotkey hk;
            hk.vk = ParseBind(entries[i].bind, &hk.modifiers);
            hk.textIdx = (int)(g_texts.size() - 1);
            if (hk.vk != 0)
                g_hotkeys.push_back(hk);
        }
    }

    g_typeLen = 0;
}
