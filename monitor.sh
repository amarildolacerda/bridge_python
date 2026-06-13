#!/bin/bash
set -e
cd "$(dirname "$0")"
source config.sh
echo "Para sair do monitor: Ctrl+]"
idf.py monitor -p /dev/ttyUSB0
