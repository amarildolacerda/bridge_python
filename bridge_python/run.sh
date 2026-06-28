#!/usr/bin/ash
set -e

# HA add-on: read options from /data/options.json and export as env vars
if [ -f /data/options.json ]; then
    export MQTT_HOST=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_host','core-mosquitto'))")
    export MQTT_PORT=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_port',1883))")
    export MQTT_USER=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_user',''))")
    export MQTT_PASS=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('mqtt_pass',''))")
    export LOG_LEVEL=$(python3 -c "import json; print(json.load(open('/data/options.json')).get('log_level','info'))")
fi

# Fallback env vars (for standalone / Docker)
export MQTT_HOST="${MQTT_HOST:-core-mosquitto}"
export MQTT_PORT="${MQTT_PORT:-1883}"
export MQTT_USER="${MQTT_USER:-}"
export MQTT_PASS="${MQTT_PASS:-}"
export LOG_LEVEL="${LOG_LEVEL:-info}"
export HTTP_PORT="${HTTP_PORT:-80}"

mkdir -p /data/bridge_python

exec python3 -m app.main
