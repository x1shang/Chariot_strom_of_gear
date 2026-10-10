@echo off
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
if not exist build mkdir build
cl /nologo /EHsc /std:c++17 /Itests\firmware-stubs /Febuild\test_dpad_firmware.exe /Fobuild\test_dpad_firmware.obj tests\test_dpad_firmware.cpp
if errorlevel 1 exit /b 1
build\test_dpad_firmware.exe
if errorlevel 1 exit /b 1
