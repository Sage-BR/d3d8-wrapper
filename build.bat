@echo off
cd /d "%~dp0"

echo [1/4] Configurando ambiente VS...
call "D:\Programas\VisualStudio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x86

echo [2/4] Gerando projeto...
premake5.exe vs2022

echo [3/4] Compilando...
set SRC=source
set CLFLAGS=/EHsc /MD /O2 /Zi /W3 /std:c++latest /I"source\dxsdk" /D_CRT_SECURE_NO_WARNINGS
set OBJDIR=build\obj\Win32\Release
set LINKFLAGS=/DLL /DEF:source\d3d8.def /OUT:data\d3d8.dll /IMPLIB:data\d3d8.lib /LIBPATH:source\dxsdk d3dx8.lib user32.lib gdi32.lib

if not exist %OBJDIR% mkdir %OBJDIR%
del /q %OBJDIR%\*.obj

cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3D8.obj %SRC%\IDirect3D8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DCubeTexture8.obj %SRC%\IDirect3DCubeTexture8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DDevice8.obj %SRC%\IDirect3DDevice8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DIndexBuffer8.obj %SRC%\IDirect3DIndexBuffer8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DSurface8.obj %SRC%\IDirect3DSurface8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DSwapChain8.obj %SRC%\IDirect3DSwapChain8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DTexture8.obj %SRC%\IDirect3DTexture8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DVertexBuffer8.obj %SRC%\IDirect3DVertexBuffer8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DVolume8.obj %SRC%\IDirect3DVolume8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\IDirect3DVolumeTexture8.obj %SRC%\IDirect3DVolumeTexture8.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\InterfaceQuery.obj %SRC%\InterfaceQuery.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\helpers.obj %SRC%\helpers.cpp
cl %CLFLAGS% /c /Fo%OBJDIR%\dllmain.obj %SRC%\dllmain.cpp

echo [4/4] Linking...
link %LINKFLAGS% %OBJDIR%\*.obj

if %ERRORLEVEL% EQU 0 (
    echo.
    echo === BUILD OK === Output: data\d3d8.dll
) else (
    echo.
    echo === ERRO ===
)

pause