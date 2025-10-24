@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
cd /d "%~dp0"
set CC=cl.exe
set CXX=cl.exe
set PATH=C:\Program Files\CMake\bin;%PATH%
cmake --preset Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe
if %ERRORLEVEL% EQU 0 (
    cmake --build ../build/Release --config Release
)