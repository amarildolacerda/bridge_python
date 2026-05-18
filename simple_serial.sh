#!/bin/bash

case "$1" in
    check)
        echo "=== DIAGNÓSTICO ==="
        lsusb | grep -i silicon
        echo ""
        ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
        echo ""
        dmesg | grep -E "(ttyUSB|cp210)" | tail -5
        ;;
    
    start)
        echo "=== ATIVANDO ==="
        sudo modprobe cp210x
        sudo modprobe usbserial
        echo "10c4 ea60" | sudo tee /sys/bus/usb-serial/drivers/cp210x/new_id
        sudo udevadm trigger
        sleep 2
        ls -la /dev/ttyUSB* /dev/ttyACM*
        ;;
    
    monitor)
        PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1)
        if [ -n "$PORT" ]; then
            sudo apt install -y minicom
            minicom -D $PORT -b 115200
        else
            echo "Nenhuma porta encontrada"
        fi
        ;;
    
    *)
        echo "Uso: $0 {check|start|monitor}"
        ;;
esac