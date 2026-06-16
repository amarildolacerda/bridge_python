#!/bin/bash
DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-/dev/ttyUSB0}"
cd "$DIR" && pio run -t upload --upload-port "$PORT"
