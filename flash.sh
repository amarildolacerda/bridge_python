#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "ESP-Matter Bridge Flasher"
    echo ""
    echo "Uso: $0 PORTA [BAUD]"
    echo ""
    echo "Exemplo: $0 /dev/ttyUSB0"
    exit 1
fi

PORT="$1"
BAUD="${2:-460800}"

echo "ESP-Matter Bridge Flasher"
echo "Porta: $PORT"
echo ""

for f in build/bootloader/bootloader.bin build/partition_table/partition-table.bin \
         build/ota_data_initial.bin build/esp_matter_bridge.bin; do
    if [ ! -f "$(dirname "$0")/$f" ]; then
        echo "ERRO: $f nao encontrado"
        exit 1
    fi
    echo "  OK: $f"
done

echo ""
echo "Flashando..."
python -m esptool --chip esp32 -b "$BAUD" --port "$PORT" \
    write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
    0x1000 "$(dirname "$0")/build/bootloader/bootloader.bin" \
    0x8000 "$(dirname "$0")/build/partition_table/partition-table.bin" \
    0x1d000 "$(dirname "$0")/build/ota_data_initial.bin" \
    0x20000 "$(dirname "$0")/build/esp_matter_bridge.bin"

echo ""
echo "Flash concluido com sucesso!"
