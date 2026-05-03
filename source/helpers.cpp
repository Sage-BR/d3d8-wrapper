#include "d3d8.h"
#include "helpers.h"
#include <iomanip>
#include <stdarg.h>

extern std::ofstream LogFile;
extern bool bEnableConsole;

void Log(const char* format, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    OutputDebugStringA(buffer);

    if (LogFile.is_open())
    {
        time_t now = time(0);
        tm ltm;
        localtime_s(&ltm, &now);
        LogFile << "[" << std::setfill('0') << std::setw(2) << ltm.tm_hour << ":"
            << std::setfill('0') << std::setw(2) << ltm.tm_min << ":"
            << std::setfill('0') << std::setw(2) << ltm.tm_sec << "] " << buffer << std::endl;
        LogFile.flush();
    }

    if (bEnableConsole)
    {
        DWORD written;
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buffer, (DWORD)strlen(buffer), &written, NULL);
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), "\n", 1, &written, NULL);
    }
}


void FrameLimiter::Init(FPSLimitMode mode)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    CachedFrequency = (double)frequency.QuadPart;
    auto TICKS_PER_SECOND = (fFPSLimit);
    if (mode == FPS_ACCURATE)
    {
        TIME_Frametime = 1000.0 / (double)fFPSLimit;
        TIME_Frequency = (double)frequency.QuadPart / 1000.0;
    }
    else
    {
        TIME_Frequency = (double)frequency.QuadPart / (double)TICKS_PER_SECOND;
    }
    Ticks();
}


DWORD FrameLimiter::Sync_RT()
{
    DWORD lastTicks, currentTicks;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    lastTicks = (DWORD)TIME_Ticks;
    TIME_Ticks = (double)counter.QuadPart / TIME_Frequency;
    currentTicks = (DWORD)TIME_Ticks;
    return (currentTicks > lastTicks) ? currentTicks - lastTicks : 0;
}

DWORD FrameLimiter::Sync_SLP()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    double millis_current = (double)counter.QuadPart / TIME_Frequency;
    double millis_delta = millis_current - TIME_Ticks;
    if (TIME_Frametime <= millis_delta)
    {
        TIME_Ticks = millis_current;
        return 1;
    }
    else if (TIME_Frametime - millis_delta > 2.0)
        Sleep(1);
    else
        Sleep(0);
    return 0;
}

void FrameLimiter::ShowFPS(LPDIRECT3DDEVICE8 device)
{
    static std::deque<int> m_times;
    LARGE_INTEGER time;
    QueryPerformanceCounter(&time);
    if (CachedFrequency == 0.0)
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        CachedFrequency = (double)frequency.QuadPart;
    }
    if (m_times.size() == 50) m_times.pop_front();
    m_times.push_back(static_cast<int>(time.QuadPart));
    uint32_t fps = 0;
    if (m_times.size() >= 2)
        fps = static_cast<uint32_t>(0.5f + (static_cast<float>(m_times.size() - 1) * static_cast<float>(CachedFrequency)) / static_cast<float>(m_times.back() - m_times.front()));

    static int space = 0;
    if (!pFPSFont || !pTimeFont)
    {
        D3DDEVICE_CREATION_PARAMETERS cparams;
        RECT rect;
        device->GetCreationParameters(&cparams);
        GetClientRect(cparams.hFocusWindow, &rect);
        LOGFONT fps_font = { rect.bottom / 20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial" };
        LOGFONT time_font = { rect.bottom / 35, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial" };
        space = rect.bottom / 20 + 5;
        D3DXCreateFontIndirect(device, &fps_font, &pFPSFont);
        D3DXCreateFontIndirect(device, &time_font, &pTimeFont);
    }
    else
    {
        auto DrawTextOutline = [](ID3DXFont* pFont, FLOAT X, FLOAT Y, D3DXCOLOR dColor, CONST PCHAR cString, ...)
        {
            const D3DXCOLOR BLACK(D3DCOLOR_XRGB(0, 0, 0));
            CHAR cBuffer[101] = "";
            va_list oArgs;
            va_start(oArgs, cString);
            _vsnprintf((cBuffer + strlen(cBuffer)), (sizeof(cBuffer) - strlen(cBuffer)), cString, oArgs);
            va_end(oArgs);
            RECT Rect[5] = { {(LONG)X - 1, (LONG)Y, (LONG)X + 500, (LONG)Y + 50}, {(LONG)X, (LONG)Y - 1, (LONG)X + 500, (LONG)Y + 50}, {(LONG)X + 1, (LONG)Y, (LONG)X + 500, (LONG)Y + 50}, {(LONG)X, (LONG)Y + 1, (LONG)X + 500, (LONG)Y + 50}, {(LONG)X, (LONG)Y, (LONG)X + 500, (LONG)Y + 50}, };
            pFont->Begin();
            if (dColor != BLACK) { for (auto i = 0; i < 4; i++) pFont->DrawText(cBuffer, -1, &Rect[i], DT_NOCLIP, BLACK); }
            pFont->DrawText(cBuffer, -1, &Rect[4], DT_NOCLIP, dColor);
            pFont->End();
        };
        static char str_format_fps[] = "%02d";
        static char str_format_time[] = "%.01f ms";
        static const D3DXCOLOR YELLOW(D3DCOLOR_XRGB(0xF7, 0xF7, 0));
        DrawTextOutline(pFPSFont, 10, 10, YELLOW, str_format_fps, fps);
        float frameTimeMs = (fps > 0) ? (1.0f / fps) * 1000.0f : 0.0f;
        DrawTextOutline(pTimeFont, 10.0f, static_cast<FLOAT>(space), YELLOW, str_format_time, frameTimeMs);
    }
}

void FrameLimiter::Ticks()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    TIME_Ticks = (double)counter.QuadPart / TIME_Frequency;
}
