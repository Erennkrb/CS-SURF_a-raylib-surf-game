@echo off
set GCC=D:\c libs\mingw64\bin\gcc.exe
set RLINC=D:\c libs\raylib-6.0_win64_mingw-w64\include
set RLLIB=D:\c libs\raylib-6.0_win64_mingw-w64\lib

set SRCJPG=%CD%\textures\Poliigon_ConcreteWorn_8690\2K\Poliigon_ConcreteWorn_8690_BaseColor.jpg
if not exist "%CD%\concrete.png" (
    if exist "%SRCJPG%" (
        echo Converting concrete texture to PNG...
        powershell -NoProfile -Command "Add-Type -AssemblyName System.Drawing; $i=[System.Drawing.Image]::FromFile('%SRCJPG%'); $i.Save('%CD%\concrete.png',[System.Drawing.Imaging.ImageFormat]::Png); $i.Dispose()"
    )
)

echo Compiling...
"%GCC%" main.c settings.c bsp.c download.c -o CS SURF.exe -O2 -Wall ^
    -I"%RLINC%" -L"%RLLIB%" ^
    -lraylib -lopengl32 -lgdi32 -lwinmm -lole32 -lurlmon

if %errorlevel%==0 (
    echo.
    echo BUILD OK - starting CS SURF.exe
    echo.
    CS SURF.exe
) else (
    echo.
    echo BUILD FAILED
    pause
)
