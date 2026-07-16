#include "settings.h"
#include "shortcuts.h"
#include "keyboard_hook.h"
#include "resource.h"
#include <commctrl.h>

#define S_WND_CLASS  L"SimpleShortcutSettings"

#define S_STYLE   (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU)
#define S_EXSTYLE (WS_EX_DLGMODALFRAME | WS_EX_TOPMOST)

static HINSTANCE g_hInst;
static HWND      g_hSettings;
static HWND      g_hPage0[4];
static HWND      g_hPage1[9];
static HWND      g_hPage2[5];
static int       g_nPage0, g_nPage1, g_nPage2;
static int       g_currentPage;
static HWND      g_hSaveBtn;
static HWND      g_hList;
static HWND      g_hEditBtn, g_hDeleteBtn;
static bool      g_hasDuplicate;
static int       g_editIndex  = -1;
static int       g_returnPage =  0;

static std::vector<ShortcutEntry> g_dupEntries;

static int       g_winW = 320, g_winH = 130;
static HFONT     g_hFont;

static void OuterSize(int cw, int ch, int* ww, int* wh)
{
    RECT r = { 0, 0, cw, ch };
    AdjustWindowRectEx(&r, S_STYLE, FALSE, S_EXSTYLE);
    *ww = r.right - r.left;
    *wh = r.bottom - r.top;
}

static void ReflowWindow()
{
    int ww, wh;
    OuterSize(g_winW, g_winH, &ww, &wh);

    RECT wrk;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wrk, 0);
    int x = wrk.left + ((wrk.right - wrk.left) - ww) / 2;
    int y = wrk.top  + ((wrk.bottom - wrk.top) - wh) / 2;
    SetWindowPos(g_hSettings, NULL, x, y, ww, wh,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ShowControls(HWND* arr, int count, int show)
{
    for (int i = 0; i < count; i++)
        if (arr[i]) ShowWindow(arr[i], show);
}

static const wchar_t* VkToName(DWORD vk)
{
    static wchar_t buf[32];
    switch (vk)
    {
    case VK_SPACE:     return L"SPACE";
    case VK_RETURN:    return L"ENTER";
    case VK_TAB:       return L"TAB";
    case VK_ESCAPE:    return L"ESCAPE";
    case VK_BACK:      return L"BACKSPACE";
    case VK_DELETE:    return L"DELETE";
    case VK_INSERT:    return L"INSERT";
    case VK_HOME:      return L"HOME";
    case VK_END:       return L"END";
    case VK_PRIOR:     return L"PGUP";
    case VK_NEXT:      return L"PGDN";
    case VK_LEFT:      return L"LEFT";
    case VK_RIGHT:     return L"RIGHT";
    case VK_UP:        return L"UP";
    case VK_DOWN:      return L"DOWN";
    case VK_SNAPSHOT:  return L"PRINT";
    case VK_SCROLL:    return L"SCROLL";
    case VK_PAUSE:     return L"PAUSE";
    case VK_CAPITAL:   return L"CAPSLOCK";
    case VK_NUMLOCK:   return L"NUMLOCK";
    case VK_MULTIPLY:  return L"MULTIPLY";
    case VK_ADD:       return L"ADD";
    case VK_SUBTRACT:  return L"SUBTRACT";
    case VK_DECIMAL:   return L"DECIMAL";
    case VK_DIVIDE:    return L"DIVIDE";
    case VK_SEPARATOR: return L"SEPARATOR";
    case VK_OEM_MINUS:    return L"MINUS";
    case VK_OEM_PLUS:     return L"PLUS";
    case VK_OEM_PERIOD:   return L"PERIOD";
    case VK_OEM_COMMA:    return L"COMMA";
    case VK_OEM_1:    return L"SEMICOLON";
    case VK_OEM_2:    return L"SLASH";
    case VK_OEM_3:    return L"TILDE";
    case VK_OEM_4:    return L"LBRACKET";
    case VK_OEM_5:    return L"BACKSLASH";
    case VK_OEM_6:    return L"RBRACKET";
    case VK_OEM_7:    return L"QUOTE";
    case VK_OEM_8:    return L"OEM_8";
    case VK_OEM_102:  return L"ANGLE";
    case VK_PACKET:   return NULL;
    }
    if (vk >= VK_F1 && vk <= VK_F24) { _snwprintf_s(buf, _TRUNCATE, L"F%lu", vk - VK_F1 + 1); return buf; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { _snwprintf_s(buf, _TRUNCATE, L"NUM%lu", vk - VK_NUMPAD0); return buf; }
    if (vk >= L'A' && vk <= L'Z') { buf[0] = (wchar_t)vk; buf[1] = 0; return buf; }
    if (vk >= L'0' && vk <= L'9') { buf[0] = (wchar_t)vk; buf[1] = 0; return buf; }
    return NULL;
}

static LRESULT CALLBACK KeybindEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR, DWORD_PTR)
{
    if (msg == WM_CHAR)
        return 0;

    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
    {
        DWORD vk = (DWORD)wParam;

        if (vk == VK_TAB || vk == VK_RETURN || vk == VK_ESCAPE)
            return DefSubclassProc(hWnd, msg, wParam, lParam);

        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
            vk == VK_LWIN || vk == VK_RWIN)
            return DefSubclassProc(hWnd, msg, wParam, lParam);

        const wchar_t* name = VkToName(vk);
        if (!name) return 0;

        bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;

        wchar_t combo[128] = L"";
        if (ctrl)  wcscat_s(combo, L"CONTROL+");
        if (alt)   wcscat_s(combo, L"ALT+");
        if (shift) wcscat_s(combo, L"SHIFT+");
        wcscat_s(combo, name);

        SetWindowTextW(hWnd, combo);
        SendMessageW(GetParent(hWnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hWnd), EN_CHANGE), (LPARAM)hWnd);
        return 0;
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYUP)
        return DefSubclassProc(hWnd, msg, wParam, lParam);
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static bool IsDuplicate(const wchar_t* shortcut, const wchar_t* bind,
                         bool* outDupShortcut, bool* outDupBind)
{
    *outDupShortcut = false;
    *outDupBind = false;
    if ((!shortcut || !shortcut[0]) && (!bind || !bind[0])) return false;

    for (size_t i = 0; i < g_dupEntries.size(); i++)
    {
        if ((int)i == g_editIndex) continue;
        if (shortcut && shortcut[0] && g_dupEntries[i].shortcut[0])
        {
            if (_wcsicmp(shortcut, g_dupEntries[i].shortcut) == 0)
                *outDupShortcut = true;
        }
        if (bind && bind[0] && g_dupEntries[i].bind[0])
        {
            if (_wcsicmp(bind, g_dupEntries[i].bind) == 0)
                *outDupBind = true;
        }
    }
    return *outDupShortcut || *outDupBind;
}

static void UpdateSaveButton()
{
    wchar_t text[4096] = {}, shortcut[128] = {}, bind[128] = {};
    GetWindowTextW(GetDlgItem(g_hSettings, IDC_EXPANDED_TEXT), text, 4096);
    GetWindowTextW(GetDlgItem(g_hSettings, IDC_SHORTCUT_TEXT), shortcut, 128);
    GetWindowTextW(GetDlgItem(g_hSettings, IDC_KEYBIND),        bind,     128);

    bool valid = text[0] != 0 && (shortcut[0] != 0 || bind[0] != 0);

    if (!valid)
    {
        EnableWindow(g_hSaveBtn, FALSE);
        g_hasDuplicate = false;
        return;
    }

    bool dupSc, dupBind;
    g_hasDuplicate = IsDuplicate(shortcut, bind, &dupSc, &dupBind);
    if (g_hasDuplicate)
    {
        SetWindowTextW(g_hSaveBtn, L"Save (dup!)");
        EnableWindow(g_hSaveBtn, TRUE);
    }
    else
    {
        SetWindowTextW(g_hSaveBtn, L"Save");
        EnableWindow(g_hSaveBtn, TRUE);
    }
}

static void RefreshManageList();
static void UpdateManageButtons();

static void SwitchToPage(int page)
{
    g_currentPage = page;

    if      (page == 0) { g_winW = 320; g_winH = 130; }
    else if (page == 1) { g_winW = 434; g_winH = 336; }
    else                { g_winW = 560; g_winH = 360; }

    ShowControls(g_hPage0, g_nPage0, page == 0 ? SW_SHOW : SW_HIDE);
    ShowControls(g_hPage1, g_nPage1, page == 1 ? SW_SHOW : SW_HIDE);
    ShowControls(g_hPage2, g_nPage2, page == 2 ? SW_SHOW : SW_HIDE);
    ReflowWindow();

    if (page == 1)
    {
        Shortcuts_Load(g_dupEntries);
        SetFocus(GetDlgItem(g_hSettings, IDC_SHORTCUT_TEXT));
        UpdateSaveButton();
    }
    else if (page == 2)
    {
        RefreshManageList();
        SetFocus(g_hList);
    }
}

static HWND MakeBtn(const wchar_t* text, int x, int y, int w, int h, int id)
{
    return CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        x, y, w, h, g_hSettings, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

static void MakePreview(const wchar_t* text, wchar_t* out, int outSize)
{
    const int maxChars = outSize - 4;
    int j = 0;
    bool truncated = false;
    for (int i = 0; text[i]; i++)
    {
        if (text[i] == L'\r' || text[i] == L'\n') { truncated = text[i + 1] != 0; break; }
        if (j >= maxChars) { truncated = true; break; }
        out[j++] = text[i];
    }
    out[j] = 0;
    if (text[0] && j == 0) truncated = true;
    if (truncated)
        wcscat_s(out, outSize, L"...");
}

static void RefreshManageList()
{
    if (!g_hList) return;

    ListView_DeleteAllItems(g_hList);

    std::vector<ShortcutEntry> entries;
    Shortcuts_Load(entries);

    for (size_t i = 0; i < entries.size(); i++)
    {
        LVITEMW item = {};
        item.mask     = LVIF_TEXT | LVIF_PARAM;
        item.iItem    = (int)i;
        item.pszText  = entries[i].shortcut[0] ? entries[i].shortcut : (LPWSTR)L"—";
        item.lParam   = (LPARAM)i;
        ListView_InsertItem(g_hList, &item);

        ListView_SetItemText(g_hList, (int)i, 1,
            entries[i].bind[0] ? entries[i].bind : (LPWSTR)L"—");

        wchar_t preview[96];
        MakePreview(entries[i].text, preview, 96);
        ListView_SetItemText(g_hList, (int)i, 2, preview);
    }

    UpdateManageButtons();
}

static void UpdateManageButtons()
{
    bool hasSel = g_hList && ListView_GetNextItem(g_hList, -1, LVNI_SELECTED) != -1;
    EnableWindow(g_hEditBtn,   hasSel);
    EnableWindow(g_hDeleteBtn, hasSel);
}

static int GetSelectedEntry()
{
    if (!g_hList) return -1;
    int sel = ListView_GetNextItem(g_hList, -1, LVNI_SELECTED);
    if (sel == -1) return -1;

    LVITEMW item = {};
    item.mask  = LVIF_PARAM;
    item.iItem = sel;
    if (!ListView_GetItem(g_hList, &item)) return -1;
    return (int)item.lParam;
}

static void CreatePage2()
{
    const int pw = 560, ph = 360;
    const int gap = 12;
    const int btnW = 88, btnH = 28;

    int listH = ph - 2 * gap - btnH - gap;
    g_hList = CreateWindowExW(0, WC_LISTVIEWW, L"",
        WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        gap, gap, pw - 2 * gap, listH,
        g_hSettings, (HMENU)(INT_PTR)IDC_SHORTCUT_LIST, g_hInst, NULL);
    ListView_SetExtendedListViewStyle(g_hList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    g_hPage2[g_nPage2++] = g_hList;

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    int listW = pw - 2 * gap - GetSystemMetrics(SM_CXVSCROLL) - 4;
    col.pszText = (LPWSTR)L"Shortcut";  col.cx = listW * 22 / 100;
    ListView_InsertColumn(g_hList, 0, &col);
    col.pszText = (LPWSTR)L"Key Bind";  col.cx = listW * 26 / 100;
    ListView_InsertColumn(g_hList, 1, &col);
    col.pszText = (LPWSTR)L"Text";      col.cx = listW * 52 / 100;
    ListView_InsertColumn(g_hList, 2, &col);

    int by = ph - gap - btnH;
    g_hPage2[g_nPage2++] = MakeBtn(L"Back", gap, by, btnW, btnH, IDC_MANAGE_BACK);
    g_hDeleteBtn = MakeBtn(L"Delete", pw - gap - btnW, by, btnW, btnH, IDC_DELETE_SHORTCUT);
    g_hPage2[g_nPage2++] = g_hDeleteBtn;
    g_hEditBtn = MakeBtn(L"Edit", pw - gap - btnW * 2 - 8, by, btnW, btnH, IDC_EDIT_SHORTCUT);
    g_hPage2[g_nPage2++] = g_hEditBtn;
    EnableWindow(g_hEditBtn, FALSE);
    EnableWindow(g_hDeleteBtn, FALSE);
}

static void BeginEditSelected()
{
    int idx = GetSelectedEntry();
    if (idx < 0) return;

    std::vector<ShortcutEntry> entries;
    if (!Shortcuts_Load(entries) || idx >= (int)entries.size()) return;

    SetWindowTextW(GetDlgItem(g_hSettings, IDC_SHORTCUT_TEXT), entries[idx].shortcut);
    SetWindowTextW(GetDlgItem(g_hSettings, IDC_KEYBIND),       entries[idx].bind);
    SetWindowTextW(GetDlgItem(g_hSettings, IDC_EXPANDED_TEXT), entries[idx].text);
    g_editIndex   = idx;
    g_returnPage  = 2;
    g_hasDuplicate = false;
    SwitchToPage(1);
}

static void DeleteSelected(HWND hWnd)
{
    int idx = GetSelectedEntry();
    if (idx < 0) return;

    std::vector<ShortcutEntry> entries;
    if (!Shortcuts_Load(entries) || idx >= (int)entries.size()) return;

    wchar_t msg[256];
    const wchar_t* label = entries[idx].shortcut[0] ? entries[idx].shortcut
                          : entries[idx].bind[0]    ? entries[idx].bind
                          : L"(no trigger)";
    _snwprintf_s(msg, _TRUNCATE, L"Delete shortcut \"%s\"?", label);
    if (MessageBoxW(hWnd, msg, L"Simple Shortcut",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    entries.erase(entries.begin() + idx);
    if (!Shortcuts_Save(entries))
    {
        MessageBoxW(hWnd, L"Could not save the shortcut file.",
                    L"Simple Shortcut", MB_OK | MB_ICONERROR);
        return;
    }
    KeyboardHook_ReloadShortcuts();
    RefreshManageList();
}

static void CreatePage0()
{
    int bw = 240, bh = 40, vgap = 12;
    int bx = (g_winW - bw) / 2;
    int by = (g_winH - (bh * 2 + vgap)) / 2;
    g_hPage0[g_nPage0++] = MakeBtn(L"Create Shortcut",  bx, by,             bw, bh, IDC_CREATE_SHORTCUT);
    g_hPage0[g_nPage0++] = MakeBtn(L"Manage Shortcuts", bx, by + bh + vgap, bw, bh, IDC_MANAGE_SHORTCUTS);
}

static void CreatePage1()
{
    HWND p = g_hSettings;
    const int pw = 434, ph = 336;
    const int gap = 12;
    const int lblH = 18, editH = 22;
    const int block = 10;
    const int ew = pw - 2 * gap;
    const int btnW = 88, btnH = 28;

    int y = gap;
    g_hPage1[g_nPage1++] = CreateWindowExW(0, L"STATIC", L"Shortcut Text:",
        WS_CHILD, gap, y, 200, lblH, p, NULL, g_hInst, NULL);
    g_hPage1[g_nPage1++] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        gap, y + lblH, ew, editH, p, (HMENU)(INT_PTR)IDC_SHORTCUT_TEXT, g_hInst, NULL);

    y += lblH + editH + block;
    g_hPage1[g_nPage1++] = CreateWindowExW(0, L"STATIC", L"Key Bind:",
        WS_CHILD, gap, y, 200, lblH, p, NULL, g_hInst, NULL);
    HWND hBind = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        gap, y + lblH, ew, editH, p, (HMENU)(INT_PTR)IDC_KEYBIND, g_hInst, NULL);
    g_hPage1[g_nPage1++] = hBind;
    SetWindowSubclass(hBind, KeybindEditProc, 200, 0);

    y += lblH + editH + block;
    g_hPage1[g_nPage1++] = CreateWindowExW(0, L"STATIC", L"Expanded Text:",
        WS_CHILD, gap, y, 200, lblH, p, NULL, g_hInst, NULL);
    int mlH = ph - (y + lblH) - (btnH + 2 * gap);
    g_hPage1[g_nPage1++] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        gap, y + lblH, ew, mlH, p, (HMENU)(INT_PTR)IDC_EXPANDED_TEXT, g_hInst, NULL);

    int by = ph - gap - btnH;
    g_hPage1[g_nPage1++] = MakeBtn(L"Back", gap, by, btnW, btnH, IDC_BACK);
    g_hSaveBtn = MakeBtn(L"Save", pw - gap - btnW, by, btnW, btnH, IDC_SAVE);
    EnableWindow(g_hSaveBtn, FALSE);
    g_hPage1[g_nPage1++] = g_hSaveBtn;
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hSettings = hWnd;

        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);
        else
            g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreatePage0();
        CreatePage1();
        CreatePage2();

        for (int i = 0; i < g_nPage1; i++)
            ShowWindow(g_hPage1[i], SW_HIDE);
        for (int i = 0; i < g_nPage2; i++)
            ShowWindow(g_hPage2[i], SW_HIDE);

        for (int i = 0; i < g_nPage0; i++) SendMessageW(g_hPage0[i], WM_SETFONT, (WPARAM)g_hFont, TRUE);
        for (int i = 0; i < g_nPage1; i++) SendMessageW(g_hPage1[i], WM_SETFONT, (WPARAM)g_hFont, TRUE);
        for (int i = 0; i < g_nPage2; i++) SendMessageW(g_hPage2[i], WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_currentPage = 0;
        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_CREATE_SHORTCUT && code == BN_CLICKED)
        {
            SetWindowTextW(GetDlgItem(hWnd, IDC_SHORTCUT_TEXT), L"");
            SetWindowTextW(GetDlgItem(hWnd, IDC_KEYBIND), L"");
            SetWindowTextW(GetDlgItem(hWnd, IDC_EXPANDED_TEXT), L"");
            g_hasDuplicate = false;
            g_editIndex   = -1;
            g_returnPage  = 0;
            SwitchToPage(1);
        }
        else if (id == IDC_MANAGE_SHORTCUTS && code == BN_CLICKED)
        {
            SwitchToPage(2);
        }
        else if (id == IDC_MANAGE_BACK && code == BN_CLICKED)
        {
            SwitchToPage(0);
        }
        else if (id == IDC_EDIT_SHORTCUT && code == BN_CLICKED)
        {
            BeginEditSelected();
        }
        else if (id == IDC_DELETE_SHORTCUT && code == BN_CLICKED)
        {
            DeleteSelected(hWnd);
        }
        else if (id == IDC_BACK && code == BN_CLICKED)
        {
            g_hasDuplicate = false;
            g_editIndex = -1;
            SwitchToPage(g_returnPage);
        }
        else if (id == IDC_SAVE && code == BN_CLICKED)
        {
            if (g_hasDuplicate)
            {
                MessageBoxW(hWnd, L"Shortcut or binding already in use.",
                            L"Simple Shortcut", MB_OK | MB_ICONWARNING);
            }
            else
            {
                ShortcutEntry e;
                memset(&e, 0, sizeof(e));
                GetWindowTextW(GetDlgItem(hWnd, IDC_EXPANDED_TEXT), e.text, 4096);
                GetWindowTextW(GetDlgItem(hWnd, IDC_SHORTCUT_TEXT), e.shortcut, 128);
                GetWindowTextW(GetDlgItem(hWnd, IDC_KEYBIND),        e.bind,     128);

                std::vector<ShortcutEntry> entries;
                Shortcuts_Load(entries);

                bool wasEdit = g_editIndex >= 0 && g_editIndex < (int)entries.size();
                if (wasEdit)
                    entries[g_editIndex] = e;
                else
                    entries.push_back(e);

                if (!Shortcuts_Save(entries))
                {
                    MessageBoxW(hWnd, L"Could not save the shortcut file.",
                                L"Simple Shortcut", MB_OK | MB_ICONERROR);
                }
                else
                {
                    KeyboardHook_ReloadShortcuts();
                    MessageBoxW(hWnd, wasEdit ? L"Shortcut updated." : L"Shortcut saved.",
                                L"Simple Shortcut", MB_OK | MB_ICONINFORMATION);
                    g_editIndex = -1;
                    SwitchToPage(g_returnPage);
                }
            }
        }
        else if ((id == IDC_SHORTCUT_TEXT || id == IDC_KEYBIND || id == IDC_EXPANDED_TEXT)
                 && code == EN_CHANGE)
        {
            UpdateSaveButton();
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->idFrom == IDC_SHORTCUT_LIST)
        {
            if (nm->code == LVN_ITEMCHANGED)
                UpdateManageButtons();
            else if (nm->code == NM_DBLCLK)
                BeginEditSelected();
            else if (nm->code == LVN_KEYDOWN)
            {
                NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lParam;
                if (kd->wVKey == VK_DELETE)
                    DeleteSelected(hWnd);
            }
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        if (g_hFont && g_hFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
            DeleteObject(g_hFont);
        g_hFont      = NULL;
        g_hSettings  = NULL;
        g_hSaveBtn   = NULL;
        g_hList      = NULL;
        g_hEditBtn   = NULL;
        g_hDeleteBtn = NULL;
        g_editIndex  = -1;
        g_returnPage = 0;
        g_dupEntries.clear();
        g_dupEntries.shrink_to_fit();
        g_nPage0 = 0;
        g_nPage1 = 0;
        g_nPage2 = 0;
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void Settings_RegisterClass(HINSTANCE hInstance)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = S_WND_CLASS;
    RegisterClassExW(&wc);
}

HWND Settings_Create(HINSTANCE hInstance, HWND hParent)
{
    if (g_hSettings)
    {
        SetForegroundWindow(g_hSettings);
        return g_hSettings;
    }

    g_hInst = hInstance;
    g_winW = 320; g_winH = 130;
    g_hasDuplicate = false;
    g_editIndex   = -1;
    g_returnPage  = 0;

    int ww, wh;
    OuterSize(g_winW, g_winH, &ww, &wh);

    RECT wrk;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wrk, 0);
    int x = wrk.left + ((wrk.right  - wrk.left) - ww) / 2;
    int y = wrk.top  + ((wrk.bottom - wrk.top)  - wh) / 2;

    return CreateWindowExW(S_EXSTYLE, S_WND_CLASS, L"Simple Shortcut",
                           S_STYLE | WS_VISIBLE, x, y, ww, wh,
                           hParent, NULL, hInstance, NULL);
}
