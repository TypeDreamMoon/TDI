// DesktopIconToggler.cpp : 双击桌面空白隐藏/显示图标 + 系统托盘切换 + 退出还原
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <oleacc.h>
#include <cstdlib>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Oleacc.lib")

// -----------------------------------------------------------------------------
// 全局变量 & 常量
// -----------------------------------------------------------------------------
static HHOOK   g_hHook             = nullptr;
static HWND    g_hDesktopListView  = nullptr;
static HWND    g_hDesktopDefView   = nullptr;
static RECT    g_desktopRect       = {};
static DWORD   g_doubleClickTime   = 0;
static int     g_cxDoubleClk       = 0;
static int     g_cyDoubleClk       = 0;

static bool    g_iconsVisible      = true;
static DWORD   g_lastClickTime     = 0;
static POINT   g_lastClickPos      = {0, 0};

static HWND    g_hWnd              = nullptr;  // 隐藏消息窗口，用于托盘

// 托盘消息 & 菜单命令 ID
const UINT WM_TRAYICON    = WM_APP + 1;
const UINT ID_TRAY_ICON   = 2001;
const UINT ID_TRAY_TOGGLE = 2002;
const UINT ID_TRAY_EXIT   = 2003;

// -----------------------------------------------------------------------------
// 功能声明
// -----------------------------------------------------------------------------
LRESULT CALLBACK LowLevelMouseProc(int, WPARAM, LPARAM);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void InitDesktopData();
void ToggleDesktopIcons();
void RestoreDesktopIcons();
bool InitTrayIcon(HINSTANCE);

// -----------------------------------------------------------------------------
// 初始化并缓存桌面视图句柄、双击参数、区域
// -----------------------------------------------------------------------------
void InitDesktopData()
{
    HWND hProgman = FindWindow(L"Progman", nullptr);
    if (!hProgman) return;

    // 先找 SHELLDLL_DefView
    HWND hDefView = FindWindowEx(hProgman, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!hDefView) {
        // Win8+ 可能在 WorkerW
        HWND hWorker = nullptr;
        while ((hWorker = FindWindowEx(nullptr, hWorker, L"WorkerW", nullptr))) {
            hDefView = FindWindowEx(hWorker, nullptr, L"SHELLDLL_DefView", nullptr);
            if (hDefView) break;
        }
    }
    if (!hDefView) return;

    g_hDesktopDefView = hDefView;

    // 再找 SysListView32（图标列表）
    HWND hList = FindWindowEx(hDefView, nullptr, L"SysListView32", nullptr);
    if (!hList) return;

    g_hDesktopListView = hList;

    // 缓存系统双击参数
    g_doubleClickTime = GetDoubleClickTime();
    g_cxDoubleClk     = GetSystemMetrics(SM_CXDOUBLECLK);
    g_cyDoubleClk     = GetSystemMetrics(SM_CYDOUBLECLK);

    // 缓存桌面图标列表的屏幕坐标
    if (!GetWindowRect(hList, &g_desktopRect)) {
        // 退而求其次，用全屏
        g_desktopRect.left   = 0;
        g_desktopRect.top    = 0;
        g_desktopRect.right  = GetSystemMetrics(SM_CXSCREEN);
        g_desktopRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
}

// -----------------------------------------------------------------------------
// 切换桌面图标可见性
// -----------------------------------------------------------------------------
void ToggleDesktopIcons()
{
    if (!g_hDesktopListView) return;
    ::ShowWindow(g_hDesktopListView,
                 g_iconsVisible ? SW_HIDE : SW_SHOW);
    g_iconsVisible = !g_iconsVisible;
}

// -----------------------------------------------------------------------------
// 退出时：恢复图标 & 删除托盘图标
// -----------------------------------------------------------------------------
void RestoreDesktopIcons()
{
    if (g_hDesktopListView && !g_iconsVisible) {
        ::ShowWindow(g_hDesktopListView, SW_SHOW);
        g_iconsVisible = true;
    }
    if (g_hWnd) {
        NOTIFYICONDATA nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd   = g_hWnd;
        nid.uID    = ID_TRAY_ICON;
        Shell_NotifyIcon(NIM_DELETE, &nid);
    }
}

// -----------------------------------------------------------------------------
// 创建 & 添加托盘图标
// -----------------------------------------------------------------------------
bool InitTrayIcon(HINSTANCE hInstance)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hWnd;
    nid.uID              = ID_TRAY_ICON;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    nid.uCallbackMessage = WM_TRAYICON;
    wcscpy_s(nid.szTip, L"Desktop Icon Toggler");
    return Shell_NotifyIcon(NIM_ADD, &nid) == TRUE;
}

// -----------------------------------------------------------------------------
// 托盘图标消息处理
// -----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TRAYICON && wParam == ID_TRAY_ICON) {
        if (lParam == WM_LBUTTONDBLCLK) {
            // 托盘双击也切换
            ToggleDesktopIcons();
        }
        else if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();

            // 第一项：根据当前状态显示文字
            if (g_iconsVisible) {
                InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING,
                           ID_TRAY_TOGGLE, L"隐藏桌面图标");
            } else {
                InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING,
                           ID_TRAY_TOGGLE, L"显示桌面图标");
            }
            // 第二项：退出
            InsertMenu(hMenu, 1, MF_BYPOSITION | MF_STRING,
                       ID_TRAY_EXIT, L"退出");

            SetForegroundWindow(hWnd);  // 保证菜单弹出后能正确关闭
            TrackPopupMenu(hMenu,
                           TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                           pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    else if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case ID_TRAY_TOGGLE:
            ToggleDesktopIcons();
            return 0;
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            return 0;
        }
    }
    else if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// 低级鼠标钩子：仅桌面双击空白切换，其它全部放行
// -----------------------------------------------------------------------------
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION
     && wParam == WM_LBUTTONDOWN
     && g_hDesktopListView)
    {
        auto pInfo = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        // —— 双击检测 —— 
        DWORD now = GetTickCount();
        int dx = abs(pInfo->pt.x - g_lastClickPos.x);
        int dy = abs(pInfo->pt.y - g_lastClickPos.y);
        bool isDouble = (now - g_lastClickTime <= g_doubleClickTime)
                     && (dx < g_cxDoubleClk)
                     && (dy < g_cyDoubleClk);
        g_lastClickTime = now;
        g_lastClickPos  = pInfo->pt;
        if (!isDouble) {
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);
        }

        // —— 只对真正的“桌面”视图处理 —— 
        HWND hUnder = WindowFromPoint(pInfo->pt);
        if (hUnder != g_hDesktopListView
         && hUnder != g_hDesktopDefView)
        {
            // 双击在其他窗口上，一律放行
            return CallNextHookEx(g_hHook, nCode, wParam, lParam);
        }

        // —— MSAA 空白检测 —— 
        IAccessible* pAcc = nullptr;
        VARIANT varChild; VariantInit(&varChild);
        bool hitIcon = false;
        if (SUCCEEDED(AccessibleObjectFromPoint(
                pInfo->pt, &pAcc, &varChild)) && pAcc)
        {
            // CHILDID_SELF 代表背景；其它值代表点击在某个图标上
            if (varChild.vt == VT_I4 && varChild.lVal != CHILDID_SELF) {
                hitIcon = true;
            }
            pAcc->Release();
            VariantClear(&varChild);
        }

        if (!hitIcon) {
            // 真正双击空白：切换隐藏/显示
            ToggleDesktopIcons();
        }
    }

    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Console 子系统时从 main 转到 wWinMain，GUI 则直接用 wWinMain
// -----------------------------------------------------------------------------
#ifdef _CONSOLE
int main()
{
    return wWinMain(GetModuleHandle(nullptr),
                    nullptr,
                    GetCommandLineW(),
                    SW_SHOWDEFAULT);
}
#endif

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE,
                      LPWSTR,
                      int)
{
    // 1) 初始化 COM（MSAA 依赖）
    CoInitialize(nullptr);
    atexit(CoUninitialize);

    // 2) 退出时自动还原 & 清托盘
    atexit(RestoreDesktopIcons);

    // 3) 缓存桌面视图数据
    InitDesktopData();
    if (!g_hDesktopListView) {
        MessageBox(nullptr,
                   L"无法定位桌面图标窗口，程序退出。",
                   L"错误",
                   MB_ICONERROR);
        return 1;
    }

    // 4) 注册后台消息窗口（用于托盘交互）
    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"DesktopIconToggler";
    RegisterClassEx(&wc);

    g_hWnd = CreateWindowEx(0,
                            wc.lpszClassName,
                            L"", 0,
                            0,0,0,0,
                            HWND_MESSAGE,
                            nullptr,
                            hInstance,
                            nullptr);
    if (!g_hWnd) {
        MessageBox(nullptr,
                   L"托盘消息窗口创建失败，程序退出。",
                   L"错误",
                   MB_ICONERROR);
        return 1;
    }

    // 5) 添加托盘图标
    if (!InitTrayIcon(hInstance)) {
        MessageBox(nullptr,
                   L"托盘图标初始化失败。",
                   L"警告",
                   MB_ICONWARNING);
    }

    // 6) 安装低级鼠标钩子（需管理员权限）
    g_hHook = SetWindowsHookEx(WH_MOUSE_LL,
                               LowLevelMouseProc,
                               nullptr,
                               0);
    if (!g_hHook) {
        MessageBox(nullptr,
                   L"安装鼠标钩子失败，请以管理员身份运行。",
                   L"错误",
                   MB_ICONERROR);
        return 1;
    }

    // 7) 隐藏控制台（如果有）
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    // 8) 消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 9) 卸钩 & 退出
    UnhookWindowsHookEx(g_hHook);
    return 0;
}
