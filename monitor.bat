@echo off
chcp 65001 >nul
setlocal

if "%1"=="" (
    echo ESP-Matter Bridge Monitor
    echo.
    echo Uso: monitor.bat PORT [BAUD]
    echo.
    echo Exemplo: monitor.bat COM4
    echo          monitor.bat COM4 115200
    exit /b 1
)

set PORT=%1
set BAUD=%2
if "%BAUD%"=="" set BAUD=115200

echo ESP-Matter Bridge Monitor
echo Porta: %PORT%  Baud: %BAUD%
echo Pressione Ctrl+] para sair
echo.

python -m serial.tools.miniterm %PORT% %BAUD%
