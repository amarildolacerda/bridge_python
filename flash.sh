#!/bin/bash
set -e
cd "$(dirname "$0")"
source config.sh
idf.py build
idf.py flash -p /dev/ttyUSB0
