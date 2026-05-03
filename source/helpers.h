#pragma once

#include <d3d8.h>
#include <d3dx8.h>
#include <deque>
#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>

// Global settings declarations
extern bool bForceWindowedMode;
extern bool bDirect3D8DisableMaximizedWindowedModeShim;
extern bool bUsePrimaryMonitor;
extern bool bCenterWindow;
extern bool bBorderlessFullscreen;
extern bool bAlwaysOnTop;
extern bool bDoNotNotifyOnTaskSwitch;
extern bool bDisplayFPSCounter;
extern float fFPSLimit;
extern int nFullScreenRefreshRateInHz;
extern bool bForceDepthStencilD24S8;
extern bool bForce32BitBackBuffer;
extern bool bSafePresent;
extern bool bForceVSync;
extern bool bFixShadowFlicker;
extern float fResolutionScale;
extern bool bGPUFocus;
void Log(const char* format, ...);


class FrameLimiter
{
public:
    enum FPSLimitMode { FPS_NONE, FPS_REALTIME, FPS_ACCURATE };
    static inline double TIME_Frequency = 0.0;
    static inline double TIME_Ticks = 0.0;
    static inline double TIME_Frametime = 0.0;
    static inline ID3DXFont* pFPSFont = nullptr;
    static inline ID3DXFont* pTimeFont = nullptr;
    static inline double CachedFrequency = 0.0;

    static void Init(FPSLimitMode mode);
    static DWORD Sync_RT();
    static DWORD Sync_SLP();
    static void ShowFPS(LPDIRECT3DDEVICE8 device);
private:
    static void Ticks();
};

extern FrameLimiter::FPSLimitMode mFPSLimitMode;

void ForceWindowed(D3DPRESENT_PARAMETERS* pPresentationParameters);
void ForceFullScreenRefreshRateInHz(D3DPRESENT_PARAMETERS* pPresentationParameters);
void ApplyCompatibilityFixes(D3DPRESENT_PARAMETERS* pPresentationParameters);

#define IsValueIntAtom(val) (HIWORD(val) == 0)

inline bool IsSystemClassNameA(LPCSTR lpClassName) {
    if (IsValueIntAtom(lpClassName)) return false;
    if (!_stricmp(lpClassName, "Button") || !_stricmp(lpClassName, "ComboBox") || 
        !_stricmp(lpClassName, "Edit") || !_stricmp(lpClassName, "ListBox") || 
        !_stricmp(lpClassName, "ScrollBar") || !_stricmp(lpClassName, "Static")) return true;
    return false;
}

inline bool IsSystemClassNameW(LPCWSTR lpClassName) {
    if (IsValueIntAtom(lpClassName)) return false;
    if (!_wcsicmp(lpClassName, L"Button") || !_wcsicmp(lpClassName, L"ComboBox") || 
        !_wcsicmp(lpClassName, L"Edit") || !_wcsicmp(lpClassName, L"ListBox") || 
        !_wcsicmp(lpClassName, L"ScrollBar") || !_wcsicmp(lpClassName, L"Static")) return true;
    return false;
}

inline bool _fnIsTopLevelWindow(HWND hWnd) {
    return GetWindow(hWnd, GW_OWNER) == NULL && GetParent(hWnd) == NULL;
}