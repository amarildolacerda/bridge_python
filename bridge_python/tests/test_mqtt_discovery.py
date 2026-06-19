from __future__ import annotations
import json
import pytest
from app.mqtt_discovery import MQTTDiscovery, build_device_info, build_entity_config
from app.models import DeviceType, BridgedDevice


@pytest.fixture
def mqtt():
    m = MQTTDiscovery(host="test", port=1883, user="", password="")
    return m


class TestMQTTDiscovery:
    def test_build_device_info(self):
        dev = BridgedDevice(id="esp8266_test", name="Test", type=DeviceType.TEMPERATURE)
        info = build_device_info(dev)
        assert info["identifiers"] == ["esp32_bridge_esp8266_test"]
        assert info["name"] == "Test"
        assert info["model"] == "temperature"

    def test_build_entity_config_sensor(self):
        dev = BridgedDevice(id="esp8266_test", name="Test", type=DeviceType.TEMPERATURE)
        config = build_entity_config(
            device_id=dev.id,
            platform="sensor",
            entity_name="temperature",
            dev=dev,
            unit="°C",
            device_class="temperature",
        )
        topic = config["~"]
        assert config["platform"] == "sensor"
        assert config["unit_of_measurement"] == "°C"
        assert config["device_class"] == "temperature"
        assert config["state_topic"] == f"{topic}/state"
        assert config["name"] == "temperature"

    def test_build_entity_config_switch(self):
        dev = BridgedDevice(id="esp8266_test", name="Test", type=DeviceType.ONOFF)
        config = build_entity_config(
            device_id=dev.id,
            platform="switch",
            entity_name="power",
            dev=dev,
        )
        assert config["platform"] == "switch"
        assert config["command_topic"] is not None

    def test_build_topic(self):
        m = MQTTDiscovery(host="h", port=1883)
        topic = m._build_topic("sensor", "esp8266_test", "temperature")
        assert topic == "homeassistant/sensor/esp8266_test/temperature/config"
