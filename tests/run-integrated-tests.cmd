@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1
set "CH_TEST_DIR=%TEMP%\chariot_integrated_tests"
if not exist "%CH_TEST_DIR%" mkdir "%CH_TEST_DIR%"
cl /nologo /EHsc /std:c++17 /Itests\integrated-stubs /Fe"%CH_TEST_DIR%\test_integrated.exe" /Fo"%CH_TEST_DIR%\test_integrated.obj" tests\test_integrated_firmware.cpp
if errorlevel 1 exit /b 1
"%CH_TEST_DIR%\test_integrated.exe"
exit /b %errorlevel%
