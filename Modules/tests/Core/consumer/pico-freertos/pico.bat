@echo off
setlocal

set "SCRIPT_PATH=%~dp0pico.py"
set "PYTHON_MODE="
set "PYTHON_PATH="

py -3 -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)" >nul 2>nul
if not errorlevel 1 (
    set "PYTHON_MODE=launcher"
    goto run_script
)

python -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)" >nul 2>nul
if not errorlevel 1 (
    set "PYTHON_MODE=executable"
    set "PYTHON_PATH=python"
    goto run_script
)

if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" (
    "%USERPROFILE%\.platformio\penv\Scripts\python.exe" -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 9) else 1)" >nul 2>nul
    if not errorlevel 1 (
        set "PYTHON_MODE=executable"
        set "PYTHON_PATH=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
        goto run_script
    )
)

echo Python 3.9 or newer is required. Tried py -3, python, and PlatformIO Python.
exit /b 1

:run_script
if "%PYTHON_MODE%"=="launcher" (
    py -3 "%SCRIPT_PATH%" %*
) else (
    "%PYTHON_PATH%" "%SCRIPT_PATH%" %*
)
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%
