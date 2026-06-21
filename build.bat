@echo off
set GCC=D:\c libs\mingw64\bin\gcc.exe
set GPP=D:\c libs\mingw64\bin\g++.exe
set RLINC=D:\c libs\raylib-6.0_win64_mingw-w64\include
set RLLIB=D:\c libs\raylib-6.0_win64_mingw-w64\lib
set STEAMINC=%CD%\sdk\public\steam
set STEAMLIB=%CD%\sdk\redistributable_bin\win64

set SRCJPG=%CD%\textures\Poliigon_ConcreteWorn_8690\2K\Poliigon_ConcreteWorn_8690_BaseColor.jpg
if not exist "%CD%\concrete.png" (
    if exist "%SRCJPG%" (
        echo Converting concrete texture to PNG...
        powershell -NoProfile -Command "Add-Type -AssemblyName System.Drawing; $i=[System.Drawing.Image]::FromFile('%SRCJPG%'); $i.Save('%CD%\concrete.png',[System.Drawing.Imaging.ImageFormat]::Png); $i.Dispose()"
    )
)

rem Keep the official Steam DLL next to the exe
if exist "%STEAMLIB%\steam_api64.dll" copy /Y "%STEAMLIB%\steam_api64.dll" "%CD%\steam_api64.dll" >nul

echo Compiling C...
"%GCC%" -c main.c settings.c bsp.c download.c console.c -O2 -Wall -I"%RLINC%"
if not %errorlevel%==0 goto fail

echo Compiling Steam module (C++)...
"%GPP%" -c net_steam.cpp -O2 -Wall -I"%STEAMINC%"
if not %errorlevel%==0 goto fail

echo Linking...
"%GPP%" main.o settings.o bsp.o download.o console.o net_steam.o -o CS-SURF.exe ^
    -L"%RLLIB%" -L"%STEAMLIB%" ^
    -lraylib -lopengl32 -lgdi32 -lwinmm -lole32 -lurlmon -l:steam_api64.dll ^
    -static-libstdc++ -static-libgcc
if not %errorlevel%==0 goto fail

echo.
echo BUILD OK - starting CS-SURF.exe
echo.
CS-SURF.exe
goto end

:fail
echo.
echo BUILD FAILED
pause

:end
