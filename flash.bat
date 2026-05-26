@echo off
chcp 65001 >nul
setlocal

if "%1"=="" (
    echo ESP-Matter Bridge Flasher
    echo.
    echo Uso: flash.bat PORT
    echo.
    echo Exemplo: flash.bat COM4
    exit /b 1
)

set PORT=%1

echo ESP-Matter Bridge Flasher
echo Porta: %PORT%
echo.

if not exist "%~dp0build\bootloader\bootloader.bin" (
    echo ERRO: build\bootloader\bootloader.bin nao encontrado
    echo Execute 'idf.py build' primeiro
    exit /b 1
)
if not exist "%~dp0build\esp_matter_bridge.bin" (
    echo ERRO: build\esp_matter_bridge.bin nao encontrado
    exit /b 1
)

echo Flashando...
python -m esptool --chip esp32 -b 460800 --port %PORT% ^
    write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m ^
    0x1000 "%~dp0build\bootloader\bootloader.bin" ^
    0x8000 "%~dp0build\partition_table\partition-table.bin" ^
    0x1d000 "%~dp0build\ota_data_initial.bin" ^
    0x20000 "%~dp0build\esp_matter_bridge.bin"

if %errorlevel% equ 0 (
    echo.
    echo Flash concluido com sucesso!
) else (
    echo.
    echo Falha no flash (codigo %errorlevel%)
    exit /b %errorlevel%
)
