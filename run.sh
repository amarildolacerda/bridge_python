#!/bin/bash
set -e

# HA add-on: read options from /data/options.json and export as env vars
if [ -f /data/options.json ]; then
    export MQTT_HOST=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_host','core-mosquitto'))")
    export MQTT_PORT=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_port',1883))")
    export MQTT_USER=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_user',''))")
    export MQTT_PASS=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_pass',''))")
    export LOG_LEVEL=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('log_level','info'))")
    export BRIDGE_IP=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('bridge_ip',''))")
fi

# Fallback env vars (for standalone / Docker)
export MQTT_HOST="${MQTT_HOST:-core-mosquitto}"
export MQTT_PORT="${MQTT_PORT:-1883}"
export MQTT_USER="${MQTT_USER:-}"
export MQTT_PASS="${MQTT_PASS:-}"
export LOG_LEVEL="${LOG_LEVEL:-info}"
export HTTP_PORT="${HTTP_PORT:-80}"
export BRIDGE_IP="${BRIDGE_IP:-}"

mkdir -p /data/home_bridge

# Auto-detect source directory (mounted addon path takes precedence)
if [ -f "/addons/home_bridge/app/main.py" ]; then
    cd /addons/home_bridge
elif [ -f "/app/app/main.py" ]; then
    cd /app
fi

# Mark all directories as safe for git (container may run as different user)
git config --global --add safe.directory '*' 2>/dev/null || true

exec python3 -m app.main
