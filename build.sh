#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

cd "$SCRIPT_DIR"

# Check if IDF is already configured
if ! command -v idf.py &>/dev/null; then
    echo "ESP-IDF not loaded. Sourcing config.sh..."
    source "$SCRIPT_DIR/config.sh"
fi

echo "Building project..."
idf.py build
echo "Build successful."
