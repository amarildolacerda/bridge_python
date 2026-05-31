@echo off
chcp 65001 >nul
setlocal

if "%1"=="" (
    echo ESP RainMaker Bridge Eraser
    echo.
    echo Uso: erase.bat PORT
    echo.
    echo Exemplo: erase.bat COM5
    exit /b 1
)

set PORT=%1

echo ESP RainMaker Bridge Eraser
echo Porta: %PORT%
echo.
echo ATENCAO: Isso vai apagar TODA a flash do ESP32!
echo.
choice /C SN /M "Confirma a operacao"
if errorlevel 2 exit /b 0

echo.
echo Apagando flash...
python -m esptool --chip esp32 --port %PORT% erase_flash

if %errorlevel% equ 0 (
    echo.
    echo Flash apagada com sucesso!
) else (
    echo.
    echo Falha ao apagar (codigo %errorlevel%)
    exit /b %errorlevel%
)
