from __future__ import annotations
import struct
import time
from typing import Any

from app.models import DeviceType

SENSOR_TYPE_MAP: dict[int, DeviceType] = {
    1: DeviceType.TEMPERATURE,
    2: DeviceType.CONTACT,
    3: DeviceType.OCCUPANCY,
    4: DeviceType.GAS,
    5: DeviceType.RAIN,
    6: DeviceType.TANQUE,
    7: DeviceType.DHT_GAS,
    8: DeviceType.ONOFF,
    9: DeviceType.LIGHT,
    11: DeviceType.ONOFF,
    12: DeviceType.SOIL_MOISTURE,
}

# Chave de estado HA para tipos relay-like
RELAY_STATE_KEY: dict[DeviceType, str] = {
    DeviceType.ONOFF: "power",
    DeviceType.LIGHT: "light",
}


def translate_ha_payload(payload: str | None) -> str | None:
    p = (payload or "").strip().lower()
    if p in ("true", "on"):
        return "on"
    if p in ("false", "off"):
        return "off"
    return None


def device_id_from_command_topic(topic: str) -> str | None:
    parts = topic.split("/")
    if len(parts) == 5 and parts[0] == "homeassistant" and parts[4] == "set":
        return parts[2]
    return None


def build_announce_bytes(bridge_ip: str, http_port: int) -> bytes:
    ip_bytes = bridge_ip.encode("utf-8")[:15].ljust(16, b"\x00")
    return struct.pack("<B4s16sH", 0x09, b"\x00\x00\x00\x00", ip_bytes, http_port)


async def handle_node_register(registry, mqtt, ws_manager, body: dict) -> tuple[dict, int]:
    device_id = body.get("device_id")
    sensor_type = body.get("sensor_type")
    device_name = body.get("device_name", device_id)
    if not device_id_or_name(device_id) or sensor_type is None:
        return {"status": "error", "message": "missing fields"}, 400
    try:
        stype = int(sensor_type)
    except (TypeError, ValueError):
        return {"status": "error", "message": "invalid sensor_type"}, 400
    dtype = SENSOR_TYPE_MAP.get(stype)
    if dtype is None:
        return {"status": "error", "message": "invalid sensor_type"}, 400
    slot = registry.register(device_id, dtype, device_name, "")
    dev = registry.get_device(device_id)
    if mqtt is not None and dev is not None:
        await mqtt.publish_device_config(dev)
    if ws_manager is not None and dev is not None:
        await ws_manager.notify_device_registered(dev)
    return {"status": "ok", "assigned_slot": slot, "device_id": device_id}, 200


async def handle_node_state(registry, ws_manager, body: dict) -> tuple[dict, int]:
    device_id = body.get("device_id")
    if not device_id:
        return {"status": "error", "message": "missing device_id"}, 400
    dev = registry.get_device(device_id)
    if dev is None:
        return {"status": "error", "message": "device not found"}, 404
    for key, value in body.items():
        if key == "device_id":
            continue
        dev.state[key] = value
    if isinstance(body.get("ip"), str):
        dev.ip = body["ip"]
    if dev.type in RELAY_STATE_KEY:
        val = None
        if "state" in body:
            val = bool(body["state"])
        elif "relay_state" in body:
            val = bool(body["relay_state"])
        if val is not None:
            dev.state[RELAY_STATE_KEY[dev.type]] = val
    dev.last_seen = time.time()
    dev.online = True
    if ws_manager is not None:
        await ws_manager.notify_device_update(dev)
    return {"status": "ok"}, 200


async def handle_node_heartbeat(registry, body: dict) -> tuple[dict, int]:
    device_id = body.get("device_id")
    if not device_id:
        return {"status": "error", "message": "missing device_id"}, 400
    dev = registry.get_device(device_id)
    if dev is None:
        return {"status": "error", "message": "device not found"}, 404
    dev.last_seen = time.time()
    dev.online = True
    return {"status": "ok"}, 200


async def handle_node_command_get(registry, device_id: str) -> tuple[dict, int]:
    if not device_id:
        return {"status": "error", "message": "missing device_id"}, 400
    dev = registry.get_device(device_id)
    if dev is None:
        return {"status": "error", "message": "device not found"}, 404
    cmd = registry.get_hub_command(device_id)
    if cmd is None:
        return {}, 200
    return {"command": cmd, "slot": registry.slot_of(device_id)}, 200


def device_id_or_name(v) -> bool:
    return bool(v)