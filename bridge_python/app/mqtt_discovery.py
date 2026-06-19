from __future__ import annotations
import json
import logging
from typing import Optional
from app.models import BridgedDevice, DeviceType

LOG = logging.getLogger(__name__)

DISCOVERY_PREFIX = "homeassistant"

# Maps device type to list of (entity_name, platform, unit, device_class, icon)
DEVICE_ENTITY_MAP: dict[DeviceType, list[tuple[str, str, str, str, str]]] = {
    DeviceType.ONOFF: [("power", "switch", "", "", "")],
    DeviceType.DIMMABLE: [("light", "light", "", "", "")],
    DeviceType.TEMPERATURE: [
        ("temperature", "sensor", "°C", "temperature", ""),
        ("humidity", "sensor", "%", "humidity", ""),
    ],
    DeviceType.HUMIDITY: [("humidity", "sensor", "%", "humidity", "")],
    DeviceType.CONTACT: [("contact", "binary_sensor", "", "door", "")],
    DeviceType.OCCUPANCY: [("occupancy", "binary_sensor", "", "occupancy", "")],
    DeviceType.LIGHT_SENSOR: [("light", "sensor", "lx", "", "mdi:brightness-5")],
    DeviceType.TANQUE: [("level", "sensor", "%", "", "mdi:water")],
    DeviceType.GAS: [
        ("alarm", "binary_sensor", "", "gas", ""),
        ("gas_level", "sensor", "%", "", "mdi:gas-cylinder"),
    ],
    DeviceType.RAIN: [
        ("rain_digital", "binary_sensor", "", "moisture", ""),
        ("rain_level", "sensor", "%", "", "mdi:weather-rainy"),
    ],
    DeviceType.ELECTRICITY: [("current", "sensor", "mA", "current", "")],
}


def build_device_info(dev: BridgedDevice) -> dict:
    return {
        "identifiers": [f"esp32_bridge_{dev.id}"],
        "name": dev.name,
        "sw_version": "bridge_python_0.1.0",
        "manufacturer": "ESP RainMaker Gateway",
        "model": dev.type.value,
        "via_device": "esp32_bridge",
    }


def build_entity_config(
    device_id: str,
    platform: str,
    entity_name: str,
    dev_name: str,
    unit: str = "",
    device_class: str = "",
    icon: str = "",
) -> dict:
    base_topic = f"{DISCOVERY_PREFIX}/{platform}/{device_id}/{entity_name}"
    config: dict = {
        "~": base_topic,
        "platform": platform,
        "name": entity_name,
        "state_topic": f"{base_topic}/state",
        "unique_id": f"esp32_bridge_{device_id}_{entity_name}",
        "device": {
            "identifiers": [f"esp32_bridge_{device_id}"],
            "name": dev_name,
            "sw_version": "bridge_python_0.1.0",
            "manufacturer": "ESP RainMaker Gateway",
            "model": "bridge_device",
            "via_device": "esp32_bridge",
        },
    }
    if unit:
        config["unit_of_measurement"] = unit
    if device_class:
        config["device_class"] = device_class
    if icon:
        config["icon"] = icon
    if platform in ("switch", "light"):
        config["command_topic"] = f"{base_topic}/set"
        config["payload_on"] = "true"
        config["payload_off"] = "false"
        config["state_on"] = "true"
        config["state_off"] = "false"
    return config


class MQTTDiscovery:
    def __init__(
        self,
        host: str = "core-mosquitto",
        port: int = 1883,
        user: str = "",
        password: str = "",
    ):
        self._host = host
        self._port = port
        self._user = user
        self._password = password
        self._client = None
        self._connected = False

    async def start(self):
        import aiomqtt
        try:
            self._client = aiomqtt.Client(
                hostname=self._host,
                port=self._port,
                username=self._user or None,
                password=self._password or None,
            )
            await self._client.__aenter__()
            self._connected = True
            LOG.info("MQTT connected to %s:%d", self._host, self._port)
        except Exception:
            LOG.warning("MQTT connection failed (will retry on device events)")
            self._connected = False

    async def stop(self):
        if self._client:
            await self._client.__aexit__(None, None, None)

    async def publish_device_config(self, dev: BridgedDevice):
        if not self._connected:
            LOG.warning("MQTT not connected, skipping discovery for %s", dev.id)
            return
        entities = DEVICE_ENTITY_MAP.get(dev.type, [])
        for entity_name, platform, unit, device_class, icon in entities:
            config = build_entity_config(
                device_id=dev.id,
                platform=platform,
                entity_name=entity_name,
                dev_name=dev.name,
                unit=unit,
                device_class=device_class,
                icon=icon,
            )
            topic = self._build_topic(platform, dev.id, entity_name)
            await self._publish(topic, json.dumps(config), retain=True)
        LOG.info("Published MQTT discovery for %s", dev.id)

    async def remove_device_config(self, dev_id: str, dev_type: DeviceType):
        if not self._connected:
            return
        entities = DEVICE_ENTITY_MAP.get(dev_type, [])
        for entity_name, platform, _, _, _ in entities:
            topic = self._build_topic(platform, dev_id, entity_name)
            await self._publish(topic, "", retain=True)
        LOG.info("Removed MQTT discovery for %s", dev_id)

    async def publish_state(self, dev: BridgedDevice):
        if not self._connected:
            return
        entities = DEVICE_ENTITY_MAP.get(dev.type, [])
        for entity_name, platform, _, _, _ in entities:
            value = dev.state.get(entity_name)
            if value is None:
                continue
            topic = self._build_state_topic(platform, dev.id, entity_name)
            # Convert bool to "true"/"false" string for MQTT
            if isinstance(value, bool):
                str_value = "true" if value else "false"
            elif isinstance(value, float):
                str_value = f"{value:.1f}"
            else:
                str_value = str(value)
            await self._publish(topic, str_value)

    async def _publish(self, topic: str, payload: str, retain: bool = False):
        if not self._client:
            return
        try:
            await self._client.publish(topic, payload=payload, retain=retain)
        except Exception:
            LOG.exception("MQTT publish failed for %s", topic)

    def _build_topic(self, platform: str, device_id: str, entity: str) -> str:
        return f"{DISCOVERY_PREFIX}/{platform}/{device_id}/{entity}/config"

    def _build_state_topic(self, platform: str, device_id: str, entity: str) -> str:
        return f"{DISCOVERY_PREFIX}/{platform}/{device_id}/{entity}/state"
