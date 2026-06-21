@echo off
REM Flash ESP RainMaker Gateway via esptool
REM Usage: flash [PORT]    (default: COM3)

set PORT=%~1
if "%PORT%"=="" set PORT=COM5

python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset ^
    write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m ^
    0x1000 build/bootloader/bootloader.bin ^
    0xc000 build/partition_table/partition-table.bin ^
    0x16000 build/ota_data_initial.bin ^
    0x20000 build/esp_rainmaker_gateway.bin
