@echo off
setlocal enabledelayedexpansion
rem mw.bat -- build / flash / log a MicroWorld ESP32 example.
rem One dispatcher for every example (no per-example duplication).
rem See examples\LOGGING.md for how logs work on the native-USB rig.

set "EXROOT=%~dp0.."
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"

if "%~1"=="" goto :usage
set "CMD=%~1"

rem --- log: view MW_LOG on a port (no example number needed) ---
if /i "%CMD%"=="log" (
  if "%~2"=="" goto :usage
  "%PY%" "%~dp0mwlog.py" %2 %3 %4
  goto :eof
)

rem --- every other command needs an example number, resolved to its folder ---
if "%~2"=="" goto :usage
set "EXDIR="
for /d %%d in ("%EXROOT%\%2-*") do set "EXDIR=%%d"
if not defined EXDIR (
  echo mw: no example folder matching "%2-*" under examples\
  exit /b 1
)

if /i "%CMD%"=="build" (
  "%PIO%" run -d "%EXDIR%"
  goto :eof
)

if /i "%CMD%"=="flash" (
  if "%~3"=="" goto :usage
  if "%~4"=="" goto :usage
  "%PIO%" run -d "%EXDIR%" -e %3 -t upload --upload-port %4
  goto :eof
)

goto :usage

:usage
echo.
echo   mw ^<command^> ...  --  build / flash / log a MicroWorld example
echo.
echo     mw build ^<NN^>                  compile all role envs of example NN
echo     mw flash ^<NN^> ^<env^> ^<COM^>     build ^& flash one env to a COM port
echo     mw log   ^<COM^> [secs] [file]   view MW_LOG on a port (Ctrl-C to stop)
echo.
echo   Examples:
echo     mw build 25
echo     mw flash 25 esp32-s3-server COM5
echo     mw flash 25 esp32-s3-client COM7
echo     mw log   COM5
echo     mw log   COM5 30 run.txt
echo.
echo   Env names live in each example's platformio.ini: single-board examples use
echo   "esp32-s3"; two-board examples use "esp32-s3-server"/"-client",
echo   "-master"/"-slave", or "-node-a"/"-node-b". See examples\LOGGING.md.
exit /b 1
