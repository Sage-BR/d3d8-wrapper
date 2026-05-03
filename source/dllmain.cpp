/**
* Copyright (C) 2020 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/

#include "d3d8.h"
#include <d3dx8.h>
#include "iathook.h"
#include "helpers.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdarg.h>

#pragma comment (lib, "d3dx8.lib")
#pragma comment (lib, "legacy_stdio_definitions.lib")
#pragma comment(lib, "winmm.lib") 

Direct3D8EnableMaximizedWindowedModeShimProc m_pDirect3D8EnableMaximizedWindowedModeShim;
ValidatePixelShaderProc m_pValidatePixelShader;
ValidateVertexShaderProc m_pValidateVertexShader;
DebugSetMuteProc m_pDebugSetMute;
Direct3DCreate8Proc m_pDirect3DCreate8;

HWND g_hFocusWindow = NULL;
HMODULE g_hWrapperModule = NULL;

bool bForceWindowedMode;
bool bDirect3D8DisableMaximizedWindowedModeShim;
bool bUsePrimaryMonitor;
bool bCenterWindow;
bool bBorderlessFullscreen;
bool bAlwaysOnTop;
bool bDoNotNotifyOnTaskSwitch;
bool bDisplayFPSCounter;
float fFPSLimit;
int nFullScreenRefreshRateInHz;

bool bForceDepthStencilD24S8;
bool bForce32BitBackBuffer;
bool bSafePresent;
bool bForceVSync;
bool bFixShadowFlicker;

float fResolutionScale = 1.0f;
bool bGPUFocus = true;

char WinDir[MAX_PATH + 1];

std::unordered_map<WORD, ULONG_PTR> WndProcMap;
std::recursive_mutex WndProcMutex;

bool bEnableLogging;
bool bEnableConsole;
std::ofstream LogFile;

FrameLimiter::FPSLimitMode mFPSLimitMode = FrameLimiter::FPSLimitMode::FPS_NONE;

void HookModule(HMODULE hmod);

template<typename T>
void HookIAT(T& orig, const char* name, void* hook, HMODULE hmod = NULL)
{
    auto ret = (T)Iat_hook::detour_iat_ptr(name, hook, hmod);
    if (ret)
    {
        if (orig == NULL) orig = ret;
        Log("Hooked: %s -> %p (Original: %p) in Module %p", name, hook, ret, hmod);
    }
}

void ForceWindowed(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    HWND hwnd = pPresentationParameters->hDeviceWindow ? pPresentationParameters->hDeviceWindow : g_hFocusWindow;
    HMONITOR monitor = MonitorFromWindow((!bUsePrimaryMonitor && hwnd) ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(monitor, &info);
    int DesktopResX = info.rcMonitor.right - info.rcMonitor.left;
    int DesktopResY = info.rcMonitor.bottom - info.rcMonitor.top;

    int left = (int)info.rcMonitor.left;
    int top = (int)info.rcMonitor.top;

    if (!bBorderlessFullscreen)
    {
        left += (int)(((float)DesktopResX / 2.0f) - ((float)pPresentationParameters->BackBufferWidth / 2.0f));
        top += (int)(((float)DesktopResY / 2.0f) - ((float)pPresentationParameters->BackBufferHeight / 2.0f));
    }

    pPresentationParameters->Windowed = 1;
    pPresentationParameters->FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    if (hwnd != NULL)
    {
        UINT uFlags = SWP_SHOWWINDOW;
        if (bBorderlessFullscreen)
        {
            LONG lOldStyle = GetWindowLong(hwnd, GWL_STYLE);
            LONG lOldExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
            LONG lNewStyle = lOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_DLGFRAME);
            lNewStyle |= (lOldStyle & WS_CHILD) ? 0 : WS_POPUP;
            LONG lNewExStyle = lOldExStyle & ~(WS_EX_CONTEXTHELP | WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW);
            lNewExStyle |= WS_EX_APPWINDOW;

            if (lNewStyle != lOldStyle)
            {
                SetWindowLong(hwnd, GWL_STYLE, lNewStyle);
                uFlags |= SWP_FRAMECHANGED;
            }
            if (lNewExStyle != lOldExStyle)
            {
                SetWindowLong(hwnd, GWL_EXSTYLE, lNewExStyle);
                uFlags |= SWP_FRAMECHANGED;
            }
            SetWindowPos(hwnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, left, top, DesktopResX, DesktopResY, uFlags);
        }
        else
        {
            if (!bCenterWindow) uFlags |= SWP_NOMOVE;
            SetWindowPos(hwnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, left, top, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, uFlags);
        }
    }
}

void ForceFullScreenRefreshRateInHz(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    if (!pPresentationParameters->Windowed)
    {
        std::vector<int> list;
        DISPLAY_DEVICE dd;
        dd.cb = sizeof(DISPLAY_DEVICE);
        DWORD deviceNum = 0;
        while (EnumDisplayDevices(NULL, deviceNum, &dd, 0))
        {
            DISPLAY_DEVICE newdd = { 0 };
            newdd.cb = sizeof(DISPLAY_DEVICE);
            DWORD monitorNum = 0;
            DEVMODE dm = { 0 };
            while (EnumDisplayDevices(dd.DeviceName, monitorNum, &newdd, 0))
            {
                for (auto iModeNum = 0; EnumDisplaySettings(NULL, iModeNum, &dm) != 0; iModeNum++)
                    list.emplace_back(dm.dmDisplayFrequency);
                monitorNum++;
            }
            deviceNum++;
        }
        std::sort(list.begin(), list.end());
        if (nFullScreenRefreshRateInHz > list.back() || nFullScreenRefreshRateInHz < list.front() || nFullScreenRefreshRateInHz < 0)
            pPresentationParameters->FullScreen_RefreshRateInHz = list.back();
        else
            pPresentationParameters->FullScreen_RefreshRateInHz = nFullScreenRefreshRateInHz;
    }
}

void ApplyCompatibilityFixes(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    if (!pPresentationParameters) return;
    Log("Applying compatibility fixes...");
    if (bForceDepthStencilD24S8 || bFixShadowFlicker)
    {
        if (pPresentationParameters->EnableAutoDepthStencil)
        {
            Log("Forcing D24S8 DepthStencil format.");
            pPresentationParameters->AutoDepthStencilFormat = D3DFMT_D24S8;
        }
    }
    if (bForce32BitBackBuffer)
    {
        Log("Forcing 32-bit BackBuffer format.");
        pPresentationParameters->BackBufferFormat = D3DFMT_X8R8G8B8;
    }
    if (bForceVSync && !pPresentationParameters->Windowed)
    {
        Log("Forcing VSync.");
        pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    }
    if (bFixShadowFlicker)
    {
        Log("Enabling FixShadowFlicker optimizations.");
        if (!pPresentationParameters->EnableAutoDepthStencil)
        {
            pPresentationParameters->EnableAutoDepthStencil = TRUE;
            pPresentationParameters->AutoDepthStencilFormat = D3DFMT_D24S8;
        }
        if (pPresentationParameters->BackBufferCount == 0) pPresentationParameters->BackBufferCount = 1;
        pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;
    }
    if (fResolutionScale != 1.0f && pPresentationParameters->BackBufferWidth > 0)
    {
        UINT oldW = pPresentationParameters->BackBufferWidth;
        UINT oldH = pPresentationParameters->BackBufferHeight;
        pPresentationParameters->BackBufferWidth = (UINT)(oldW * fResolutionScale);
        pPresentationParameters->BackBufferHeight = (UINT)(oldH * fResolutionScale);
        Log("ResolutionScale: Scaled %ux%u -> %ux%u", oldW, oldH, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight);
    }
}

HRESULT m_IDirect3D8::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice8** ppReturnedDeviceInterface)
{
    g_hFocusWindow = hFocusWindow ? hFocusWindow : pPresentationParameters->hDeviceWindow;
    if (bForceWindowedMode) ForceWindowed(pPresentationParameters);
    if (nFullScreenRefreshRateInHz) ForceFullScreenRefreshRateInHz(pPresentationParameters);
    ApplyCompatibilityFixes(pPresentationParameters);

    if (bDisplayFPSCounter) { if (FrameLimiter::pFPSFont) FrameLimiter::pFPSFont->Release(); if (FrameLimiter::pTimeFont) FrameLimiter::pTimeFont->Release(); FrameLimiter::pFPSFont = nullptr; FrameLimiter::pTimeFont = nullptr; }

    HRESULT hr = ProxyInterface->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    if (SUCCEEDED(hr) && ppReturnedDeviceInterface)
    {
        *ppReturnedDeviceInterface = new m_IDirect3DDevice8(*ppReturnedDeviceInterface, this);
    }
    return hr;
}

LRESULT WINAPI CustomWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, WNDPROC oWndProc)
{
    if (hWnd == g_hFocusWindow || _fnIsTopLevelWindow(hWnd)) 
    {
        if (bAlwaysOnTop)
        {
            if ((GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0)
                SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        }
        if (bDoNotNotifyOnTaskSwitch)
        {
            switch (uMsg)
            {
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE)
                {
                    if ((HWND)lParam == NULL) return 0;
                    DWORD dwPID = 0;
                    GetWindowThreadProcessId((HWND)lParam, &dwPID);
                    if (dwPID != GetCurrentProcessId()) return 0;
                }
                break;
            case WM_NCACTIVATE: if (LOWORD(wParam) == WA_INACTIVE) return 0; break;
            case WM_ACTIVATEAPP: if (wParam == FALSE) return 0; break;
            case WM_KILLFOCUS:
            {
                if ((HWND)wParam == NULL) return 0;
                DWORD dwPID = 0;
                GetWindowThreadProcessId((HWND)wParam, &dwPID);
                if (dwPID != GetCurrentProcessId()) return 0;
            }
            break;
            default: break;
            }
        }
    }
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

LRESULT WINAPI CustomWndProcA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WORD wClassAtom = GetClassWord(hWnd, GCW_ATOM);
    if (wClassAtom)
    {
        std::lock_guard<std::recursive_mutex> lock(WndProcMutex);
        auto it = WndProcMap.find(wClassAtom);
        if (it != WndProcMap.end()) return CustomWndProc(hWnd, uMsg, wParam, lParam, (WNDPROC)it->second);
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

LRESULT WINAPI CustomWndProcW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    WORD wClassAtom = GetClassWord(hWnd, GCW_ATOM);
    if (wClassAtom)
    {
        std::lock_guard<std::recursive_mutex> lock(WndProcMutex);
        auto it = WndProcMap.find(wClassAtom);
        if (it != WndProcMap.end()) return CustomWndProc(hWnd, uMsg, wParam, lParam, (WNDPROC)it->second);
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

typedef BOOL(__stdcall* UnregisterClassA_fn)(LPCSTR, HINSTANCE);
typedef BOOL(__stdcall* UnregisterClassW_fn)(LPCWSTR, HINSTANCE);
UnregisterClassA_fn oUnregisterClassA = NULL;
UnregisterClassW_fn oUnregisterClassW = NULL;

BOOL __stdcall hk_UnregisterClassA(LPCSTR lpClassName, HINSTANCE hInstance)
{
    if (IsValueIntAtom(DWORD(lpClassName))) {
        WORD wClassAtom = LOWORD(lpClassName);
        std::lock_guard<std::recursive_mutex> lock(WndProcMutex);
        WndProcMap.erase(wClassAtom);
    }
    return oUnregisterClassA(lpClassName, hInstance);
}

BOOL __stdcall hk_UnregisterClassW(LPCWSTR lpClassName, HINSTANCE hInstance)
{
    if (IsValueIntAtom(DWORD(lpClassName))) {
        WORD wClassAtom = LOWORD(lpClassName);
        std::lock_guard<std::recursive_mutex> lock(WndProcMutex);
        WndProcMap.erase(wClassAtom);
    }
    return oUnregisterClassW(lpClassName, hInstance);
}

typedef ATOM(__stdcall* RegisterClassA_fn)(const WNDCLASSA*);
typedef ATOM(__stdcall* RegisterClassW_fn)(const WNDCLASSW*);
typedef ATOM(__stdcall* RegisterClassExA_fn)(const WNDCLASSEXA*);
typedef ATOM(__stdcall* RegisterClassExW_fn)(const WNDCLASSEXW*);
RegisterClassA_fn oRegisterClassA = NULL;
RegisterClassW_fn oRegisterClassW = NULL;
RegisterClassExA_fn oRegisterClassExA = NULL;
RegisterClassExW_fn oRegisterClassExW = NULL;

ATOM __stdcall hk_RegisterClassA(WNDCLASSA* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { if (IsSystemClassNameA(lpWndClass->lpszClassName)) return oRegisterClassA(lpWndClass); }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcA;
    WORD wClassAtom = oRegisterClassA(lpWndClass);
    if (wClassAtom != 0) { std::lock_guard<std::recursive_mutex> lock(WndProcMutex); WndProcMap[wClassAtom] = pWndProc; }
    return wClassAtom;
}

ATOM __stdcall hk_RegisterClassW(WNDCLASSW* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { if (IsSystemClassNameW(lpWndClass->lpszClassName)) return oRegisterClassW(lpWndClass); }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcW;
    WORD wClassAtom = oRegisterClassW(lpWndClass);
    if (wClassAtom != 0) { std::lock_guard<std::recursive_mutex> lock(WndProcMutex); WndProcMap[wClassAtom] = pWndProc; }
    return wClassAtom;
}

ATOM __stdcall hk_RegisterClassExA(WNDCLASSEXA* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { if (IsSystemClassNameA(lpWndClass->lpszClassName)) return oRegisterClassExA(lpWndClass); }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcA;
    WORD wClassAtom = oRegisterClassExA(lpWndClass);
    if (wClassAtom != 0) { std::lock_guard<std::recursive_mutex> lock(WndProcMutex); WndProcMap[wClassAtom] = pWndProc; }
    return wClassAtom;
}

ATOM __stdcall hk_RegisterClassExW(WNDCLASSEXW* lpWndClass)
{
    if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { if (IsSystemClassNameW(lpWndClass->lpszClassName)) return oRegisterClassExW(lpWndClass); }
    ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
    lpWndClass->lpfnWndProc = CustomWndProcW;
    WORD wClassAtom = oRegisterClassExW(lpWndClass);
    if (wClassAtom != 0) { std::lock_guard<std::recursive_mutex> lock(WndProcMutex); WndProcMap[wClassAtom] = pWndProc; }
    return wClassAtom;
}

typedef HWND(__stdcall* GetForegroundWindow_fn)(void);
GetForegroundWindow_fn oGetForegroundWindow = NULL;
HWND __stdcall hk_GetForegroundWindow() { if (bDoNotNotifyOnTaskSwitch && g_hFocusWindow && IsWindow(g_hFocusWindow)) return g_hFocusWindow; return oGetForegroundWindow(); }

typedef HWND(__stdcall* GetActiveWindow_fn)(void);
GetActiveWindow_fn oGetActiveWindow = NULL;
HWND __stdcall hk_GetActiveWindow(void) { HWND hWndActive = oGetActiveWindow(); if (bDoNotNotifyOnTaskSwitch && g_hFocusWindow && hWndActive == NULL && IsWindow(g_hFocusWindow)) { if (GetCurrentThreadId() == GetWindowThreadProcessId(g_hFocusWindow, NULL)) return g_hFocusWindow; } return hWndActive; }

typedef HWND(__stdcall* GetFocus_fn)(void);
GetFocus_fn oGetFocus = NULL;
HWND __stdcall hk_GetFocus(void) { HWND hWndFocus = oGetFocus(); if (bDoNotNotifyOnTaskSwitch && g_hFocusWindow && hWndFocus == NULL && IsWindow(g_hFocusWindow)) { if (GetCurrentThreadId() == GetWindowThreadProcessId(g_hFocusWindow, NULL)) return g_hFocusWindow; } return hWndFocus; }

typedef HMODULE(__stdcall* LoadLibraryA_fn)(LPCSTR lpLibFileName);
LoadLibraryA_fn oLoadLibraryA;
HMODULE __stdcall hk_LoadLibraryA(LPCSTR lpLibFileName) { HMODULE hmod = oLoadLibraryA(lpLibFileName); if (hmod) HookModule(hmod); return hmod; }

typedef HMODULE(__stdcall* LoadLibraryW_fn)(LPCWSTR lpLibFileName);
LoadLibraryW_fn oLoadLibraryW;
HMODULE __stdcall hk_LoadLibraryW(LPCWSTR lpLibFileName) { HMODULE hmod = oLoadLibraryW(lpLibFileName); if (hmod) HookModule(hmod); return hmod; }

typedef HMODULE(__stdcall* LoadLibraryExA_fn)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
LoadLibraryExA_fn oLoadLibraryExA;
HMODULE __stdcall hk_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) { HMODULE hmod = oLoadLibraryExA(lpLibFileName, hFile, dwFlags); if (hmod && ((dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) == 0)) HookModule(hmod); return hmod; }

typedef HMODULE(__stdcall* LoadLibraryExW_fn)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
LoadLibraryExW_fn oLoadLibraryExW;
HMODULE __stdcall hk_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) { HMODULE hmod = oLoadLibraryExW(lpLibFileName, hFile, dwFlags); if (hmod && ((dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) == 0)) HookModule(hmod); return hmod; }

typedef BOOL(__stdcall* FreeLibrary_fn)(HMODULE hLibModule);
FreeLibrary_fn oFreeLibrary;
BOOL __stdcall hk_FreeLibrary(HMODULE hLibModule) { if (hLibModule == g_hWrapperModule) return TRUE; return oFreeLibrary(hLibModule); }

FARPROC __stdcall hk_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    if (!HIWORD(lpProcName)) return GetProcAddress(hModule, lpProcName);
    __try
    {
        if (!lstrcmpA(lpProcName, "RegisterClassA")) { if (oRegisterClassA == NULL) oRegisterClassA = (RegisterClassA_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_RegisterClassA; }
        if (!lstrcmpA(lpProcName, "RegisterClassW")) { if (oRegisterClassW == NULL) oRegisterClassW = (RegisterClassW_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_RegisterClassW; }
        if (!lstrcmpA(lpProcName, "RegisterClassExA")) { if (oRegisterClassExA == NULL) oRegisterClassExA = (RegisterClassExA_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_RegisterClassExA; }
        if (!lstrcmpA(lpProcName, "RegisterClassExW")) { if (oRegisterClassExW == NULL) oRegisterClassExW = (RegisterClassExW_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_RegisterClassExW; }
        if (!lstrcmpA(lpProcName, "UnregisterClassA")) { if (oUnregisterClassA == NULL) oUnregisterClassA = (UnregisterClassA_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_UnregisterClassA; }
        if (!lstrcmpA(lpProcName, "UnregisterClassW")) { if (oUnregisterClassW == NULL) oUnregisterClassW = (UnregisterClassW_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_UnregisterClassW; }
        if (!lstrcmpA(lpProcName, "GetForegroundWindow")) { if (oGetForegroundWindow == NULL) oGetForegroundWindow = (GetForegroundWindow_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_GetForegroundWindow; }
        if (!lstrcmpA(lpProcName, "GetActiveWindow")) { if (oGetActiveWindow == NULL) oGetActiveWindow = (GetActiveWindow_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_GetActiveWindow; }
        if (!lstrcmpA(lpProcName, "GetFocus")) { if (oGetFocus == NULL) oGetFocus = (GetFocus_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_GetFocus; }
        if (!lstrcmpA(lpProcName, "LoadLibraryA")) { if (oLoadLibraryA == NULL) oLoadLibraryA = (LoadLibraryA_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_LoadLibraryA; }
        if (!lstrcmpA(lpProcName, "LoadLibraryW")) { if (oLoadLibraryW == NULL) oLoadLibraryW = (LoadLibraryW_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_LoadLibraryW; }
        if (!lstrcmpA(lpProcName, "LoadLibraryExA")) { if (oLoadLibraryExA == NULL) oLoadLibraryExA = (LoadLibraryExA_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_LoadLibraryExA; }
        if (!lstrcmpA(lpProcName, "LoadLibraryExW")) { if (oLoadLibraryExW == NULL) oLoadLibraryExW = (LoadLibraryExW_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_LoadLibraryExW; }
        if (!lstrcmpA(lpProcName, "FreeLibrary")) { if (oFreeLibrary == NULL) oFreeLibrary = (FreeLibrary_fn)GetProcAddress(hModule, lpProcName); return (FARPROC)hk_FreeLibrary; }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return GetProcAddress(hModule, lpProcName);
}

void HookModule(HMODULE hmod)
{
    if (hmod == g_hWrapperModule) return;
    char modpath[MAX_PATH + 1];
    if (GetModuleFileNameA(hmod, modpath, MAX_PATH)) { if (!_strnicmp(modpath, WinDir, strlen(WinDir))) return; }
    Log("Hooking module: %p (%s)", hmod, modpath);
    HookIAT(oRegisterClassA, "RegisterClassA", (void*)hk_RegisterClassA, hmod);
    HookIAT(oRegisterClassW, "RegisterClassW", (void*)hk_RegisterClassW, hmod);
    HookIAT(oRegisterClassExA, "RegisterClassExA", (void*)hk_RegisterClassExA, hmod);
    HookIAT(oRegisterClassExW, "RegisterClassExW", (void*)hk_RegisterClassExW, hmod);
    HookIAT(oUnregisterClassA, "UnregisterClassA", (void*)hk_UnregisterClassA, hmod);
    HookIAT(oUnregisterClassW, "UnregisterClassW", (void*)hk_UnregisterClassW, hmod);
    HookIAT(oGetForegroundWindow, "GetForegroundWindow", (void*)hk_GetForegroundWindow, hmod);
    HookIAT(oGetActiveWindow, "GetActiveWindow", (void*)hk_GetActiveWindow, hmod);
    HookIAT(oGetFocus, "GetFocus", (void*)hk_GetFocus, hmod);
    HookIAT(oLoadLibraryA, "LoadLibraryA", (void*)hk_LoadLibraryA, hmod);
    HookIAT(oLoadLibraryW, "LoadLibraryW", (void*)hk_LoadLibraryW, hmod);
    HookIAT(oLoadLibraryExA, "LoadLibraryExA", (void*)hk_LoadLibraryExA, hmod);
    HookIAT(oLoadLibraryExW, "LoadLibraryExW", (void*)hk_LoadLibraryExW, hmod);
    HookIAT(oFreeLibrary, "FreeLibrary", (void*)hk_FreeLibrary, hmod);
    Iat_hook::detour_iat_ptr("GetProcAddress", (void*)hk_GetProcAddress, hmod);
}

void HookImportedModules()
{
    HMODULE hModule = GetModuleHandle(0);
    PIMAGE_DOS_HEADER img_dos_headers = (PIMAGE_DOS_HEADER)hModule;
    PIMAGE_NT_HEADERS img_nt_headers = (PIMAGE_NT_HEADERS)((BYTE*)img_dos_headers + img_dos_headers->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR img_import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)img_dos_headers + img_nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    if (img_dos_headers->e_magic != IMAGE_DOS_SIGNATURE) return;
    for (IMAGE_IMPORT_DESCRIPTOR* iid = img_import_desc; iid->Name != 0; iid++) {
        char* mod_name = (char*)((size_t*)(iid->Name + (size_t)hModule));
        HMODULE hm = GetModuleHandleA(mod_name);
        if (hm && !(GetProcAddress(hm, "DirectInput8Create") != NULL && GetProcAddress(hm, "DirectSoundCreate8") != NULL && GetProcAddress(hm, "InternetOpenA") != NULL)) HookModule(hm);
    }
}

bool WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
    static HMODULE d3d8dll = nullptr;
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            g_hWrapperModule = hModule;
            char path[MAX_PATH];
            GetSystemDirectoryA(path, MAX_PATH);
            strcat_s(path, "\\d3d8.dll");
            d3d8dll = LoadLibraryA(path);
            m_pDirect3D8EnableMaximizedWindowedModeShim = (Direct3D8EnableMaximizedWindowedModeShimProc)GetProcAddress(d3d8dll, "Direct3D8EnableMaximizedWindowedModeShim");
            m_pValidatePixelShader = (ValidatePixelShaderProc)GetProcAddress(d3d8dll, "ValidatePixelShader");
            m_pValidateVertexShader = (ValidateVertexShaderProc)GetProcAddress(d3d8dll, "ValidateVertexShader");
            m_pDebugSetMute = (DebugSetMuteProc)GetProcAddress(d3d8dll, "DebugSetMute");
            m_pDirect3DCreate8 = (Direct3DCreate8Proc)GetProcAddress(d3d8dll, "Direct3DCreate8");

            HMODULE hm = NULL;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Direct3DCreate8, &hm);
            GetModuleFileNameA(hm, path, sizeof(path));
            char* lastBackslash = strrchr(path, '\\');
            if (lastBackslash) { static const char iniSuffix[] = "\\d3d8.ini"; if ((lastBackslash - path) + sizeof(iniSuffix) <= sizeof(path)) memcpy(lastBackslash, iniSuffix, sizeof(iniSuffix)); }

            bForceWindowedMode = GetPrivateProfileInt("MAIN", "ForceWindowedMode", 0, path) != 0;
            bDirect3D8DisableMaximizedWindowedModeShim = GetPrivateProfileInt("MAIN", "Direct3D8DisableMaximizedWindowedModeShim", 0, path) != 0;
            fFPSLimit = static_cast<float>(GetPrivateProfileInt("MAIN", "FPSLimit", 0, path));
            nFullScreenRefreshRateInHz = GetPrivateProfileInt("MAIN", "FullScreenRefreshRateInHz", 0, path);
            bDisplayFPSCounter = GetPrivateProfileInt("MAIN", "DisplayFPSCounter", 0, path);
            bUsePrimaryMonitor = GetPrivateProfileInt("FORCEWINDOWED", "UsePrimaryMonitor", 0, path) != 0;
            bCenterWindow = GetPrivateProfileInt("FORCEWINDOWED", "CenterWindow", 1, path) != 0;
            bBorderlessFullscreen = GetPrivateProfileInt("FORCEWINDOWED", "BorderlessFullscreen", 0, path) != 0;
            bAlwaysOnTop = GetPrivateProfileInt("FORCEWINDOWED", "AlwaysOnTop", 0, path) != 0;
            bDoNotNotifyOnTaskSwitch = GetPrivateProfileInt("FORCEWINDOWED", "DoNotNotifyOnTaskSwitch", 0, path) != 0;
            bForceDepthStencilD24S8 = GetPrivateProfileInt("COMPATIBILITY", "ForceDepthStencilD24S8", 0, path) != 0;
            bForce32BitBackBuffer = GetPrivateProfileInt("COMPATIBILITY", "Force32BitBackBuffer", 0, path) != 0;
            bSafePresent = GetPrivateProfileInt("COMPATIBILITY", "SafePresent", 1, path) != 0;
            fResolutionScale = (float)GetPrivateProfileInt("MAIN", "ResolutionScale", 100, path) / 100.0f;
            if (fResolutionScale <= 0.1f) fResolutionScale = 1.0f;
            bGPUFocus = GetPrivateProfileInt("MAIN", "GPUFocus", 1, path) != 0;
            if (bGPUFocus) { SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST); }
            bForceVSync = GetPrivateProfileInt("COMPATIBILITY", "ForceVSync", 0, path) != 0;
            bFixShadowFlicker = GetPrivateProfileInt("COMPATIBILITY", "FixShadowFlicker", 0, path) != 0;
            bEnableLogging = GetPrivateProfileInt("LOG", "EnableLogging", 1, path) != 0;
            bEnableConsole = GetPrivateProfileInt("LOG", "EnableConsole", 0, path) != 0;

            if (bEnableLogging && !LogFile.is_open())
            {
                char logPath[MAX_PATH]; strcpy(logPath, path);
                char* lastPart = strrchr(logPath, '.'); if (lastPart) strcpy(lastPart, ".log");
                LogFile.open(logPath, std::ios::out | std::ios::trunc);
                Log("--- d3d8-wrapper Log Started ---");
            }
            if (bEnableConsole) { AllocConsole(); freopen("CONOUT$", "w", stdout); Log("Console Allocated."); }
            Log("INI Loaded: %s", path);
            if (fFPSLimit > 0.0f) {
                FrameLimiter::FPSLimitMode mode = (GetPrivateProfileInt("MAIN", "FPSLimitMode", 1, path) == 2) ? FrameLimiter::FPSLimitMode::FPS_ACCURATE : FrameLimiter::FPSLimitMode::FPS_REALTIME;
                if (mode == FrameLimiter::FPSLimitMode::FPS_ACCURATE) timeBeginPeriod(1);
                FrameLimiter::Init(mode); mFPSLimitMode = mode;
            } else mFPSLimitMode = FrameLimiter::FPSLimitMode::FPS_NONE;

            if (bDirect3D8DisableMaximizedWindowedModeShim) {
                auto addr = (uintptr_t)GetProcAddress(d3d8dll, "Direct3D8EnableMaximizedWindowedModeShim");
                if (addr) {
                    DWORD Protect; VirtualProtect((LPVOID)(addr + 6), 4, PAGE_EXECUTE_READWRITE, &Protect);
                    *(unsigned*)(addr + 6) = 0; *(unsigned*)(*(unsigned*)(addr + 2)) = 0;
                    VirtualProtect((LPVOID)(addr + 6), 4, Protect, &Protect); bForceWindowedMode = false;
                }
            }
            GetSystemWindowsDirectoryA(WinDir, MAX_PATH);
            HookIAT(oRegisterClassA, "RegisterClassA", (void*)hk_RegisterClassA);
            HookIAT(oRegisterClassW, "RegisterClassW", (void*)hk_RegisterClassW);
            HookIAT(oRegisterClassExA, "RegisterClassExA", (void*)hk_RegisterClassExA);
            HookIAT(oRegisterClassExW, "RegisterClassExW", (void*)hk_RegisterClassExW);
            HookIAT(oGetForegroundWindow, "GetForegroundWindow", (void*)hk_GetForegroundWindow);
            HookIAT(oGetActiveWindow, "GetActiveWindow", (void*)hk_GetActiveWindow);
            HookIAT(oGetFocus, "GetFocus", (void*)hk_GetFocus);
            HookIAT(oLoadLibraryA, "LoadLibraryA", (void*)hk_LoadLibraryA);
            HookIAT(oLoadLibraryW, "LoadLibraryW", (void*)hk_LoadLibraryW);
            HookIAT(oLoadLibraryExA, "LoadLibraryExA", (void*)hk_LoadLibraryExA);
            HookIAT(oLoadLibraryExW, "LoadLibraryExW", (void*)hk_LoadLibraryExW);
            HookIAT(oFreeLibrary, "FreeLibrary", (void*)hk_FreeLibrary);
            Iat_hook::detour_iat_ptr("GetProcAddress", (void*)hk_GetProcAddress);
            Iat_hook::detour_iat_ptr("GetProcAddress", (void*)hk_GetProcAddress, d3d8dll);
            HookIAT(oGetForegroundWindow, "GetForegroundWindow", (void*)hk_GetForegroundWindow, d3d8dll);
            HMODULE ole32 = GetModuleHandleA("ole32.dll");
            if (ole32) {
                HookIAT(oRegisterClassA, "RegisterClassA", (void*)hk_RegisterClassA, ole32); HookIAT(oRegisterClassW, "RegisterClassW", (void*)hk_RegisterClassW, ole32);
                HookIAT(oRegisterClassExA, "RegisterClassExA", (void*)hk_RegisterClassExA, ole32); HookIAT(oRegisterClassExW, "RegisterClassExW", (void*)hk_RegisterClassExW, ole32);
                HookIAT(oGetActiveWindow, "GetActiveWindow", (void*)hk_GetActiveWindow, ole32);
            }
            HookImportedModules();
        }
        break;
        case DLL_PROCESS_DETACH:
            Log("--- d3d8-wrapper Unloading ---");
            if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_ACCURATE) timeEndPeriod(1);
            if (LogFile.is_open()) LogFile.close();
            if (d3d8dll) FreeLibrary(d3d8dll);
        break;
    }
    return true;
}

int WINAPI Direct3D8EnableMaximizedWindowedModeShim(BOOL mEnable) { if (!m_pDirect3D8EnableMaximizedWindowedModeShim) return E_FAIL; return m_pDirect3D8EnableMaximizedWindowedModeShim(mEnable); }
HRESULT WINAPI ValidatePixelShader(DWORD* pixelshader, DWORD* reserved1, BOOL flag, DWORD* toto) { if (!m_pValidatePixelShader) return E_FAIL; return m_pValidatePixelShader(pixelshader, reserved1, flag, toto); }
HRESULT WINAPI ValidateVertexShader(DWORD* vertexshader, DWORD* reserved1, DWORD* reserved2, BOOL flag, DWORD* toto) { if (!m_pValidateVertexShader) return E_FAIL; return m_pValidateVertexShader(vertexshader, reserved1, reserved2, flag, toto); }
void WINAPI DebugSetMute() { if (!m_pDebugSetMute) return; m_pDebugSetMute(); }
IDirect3D8 *WINAPI Direct3DCreate8(UINT SDKVersion) {
    Log("Direct3DCreate8 called (SDKVersion: %u)", SDKVersion);
    if (!m_pDirect3DCreate8) return nullptr;
    IDirect3D8 *pD3D8 = m_pDirect3DCreate8(SDKVersion);
    if (pD3D8) return new m_IDirect3D8(pD3D8);
    return nullptr;
}
