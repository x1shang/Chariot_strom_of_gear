@echo off
cd /d "%~dp0.."
if not exist build mkdir build
g++ -std=c++17 -Itests/firmware-stubs tests/test_combined_firmware.cpp -o build/test_combined_firmware.exe
if errorlevel 1 exit /b 1
build\test_combined_firmware.exe
if errorlevel 1 exit /b 1
