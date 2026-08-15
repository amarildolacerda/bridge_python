import pytest
from app.models import DeviceType
from app.mqtt_discovery import DEVICE_ENTITY_MAP


def test_light_device_type_exists():
    assert DeviceType.LIGHT.value == "light"


def test_light_entity_map_entry():
    assert DEVICE_ENTITY_MAP[DeviceType.LIGHT] == [("light", "light", "", "", "")]


from app.device_registry import DeviceRegistry


def test_hub_command_queue(tmp_path):
    reg = DeviceRegistry(data_dir=str(tmp_path))
    reg.load()
    reg.register("agri_123", DeviceType.ONOFF, "Lamp", "")
    assert reg.enqueue_hub_command("agri_123", "on") is True
    assert reg.enqueue_hub_command("nope", "on") is False
    assert reg.get_hub_command("agri_123") == "on"
    assert reg.get_hub_command("agri_123") is None
    assert reg.slot_of("agri_123") == 0
    assert reg.slot_of("missing") == -1


def test_hub_command_queue_cap(tmp_path):
    reg = DeviceRegistry(data_dir=str(tmp_path))
    reg.load()
    reg.register("agri_cap", DeviceType.ONOFF, "Cap", "")
    for i in range(15):
        reg.enqueue_hub_command("agri_cap", f"c{i}")
    # cap em 10: os 5 primeiros foram descartados
    assert reg.get_hub_command("agri_cap") == "c5"


from unittest.mock import AsyncMock
from app import hub_compat


@pytest.mark.asyncio
async def test_register_state_command_cycle(tmp_path):
    reg = DeviceRegistry(data_dir=str(tmp_path))
    reg.load()
    mqtt = AsyncMock()
    ws = AsyncMock()
    resp, code = await hub_compat.handle_node_register(
        reg, mqtt, ws,
        {"device_id": "agri_abc", "sensor_type": 9, "device_name": "Lamp"},
    )
    assert code == 200
    assert resp["assigned_slot"] == 0
    assert resp["device_id"] == "agri_abc"
    mqtt.publish_device_config.assert_awaited_once()

    resp, code = await hub_compat.handle_node_state(
        reg, ws,
        {"device_id": "agri_abc", "relay_state": True, "ip": "1.2.3.4"},
    )
    assert code == 200
    dev = reg.get_device("agri_abc")
    assert dev.state["light"] is True
    assert dev.ip == "1.2.3.4"

    reg.enqueue_hub_command("agri_abc", "on")
    resp, code = await hub_compat.handle_node_command_get(reg, "agri_abc")
    assert code == 200
    assert resp == {"command": "on", "slot": 0}

    resp, code = await hub_compat.handle_node_command_get(reg, "agri_abc")
    assert resp == {}


@pytest.mark.asyncio
async def test_invalid_sensor_type(tmp_path):
    reg = DeviceRegistry(data_dir=str(tmp_path))
    reg.load()
    resp, code = await hub_compat.handle_node_register(
        reg, None, None, {"device_id": "x", "sensor_type": 99}
    )
    assert code == 400


def test_translate_and_topic():
    assert hub_compat.translate_ha_payload("true") == "on"
    assert hub_compat.translate_ha_payload("FALSE") == "off"
    assert hub_compat.translate_ha_payload("on") == "on"
    assert hub_compat.translate_ha_payload("bogus") is None
    assert hub_compat.device_id_from_command_topic("homeassistant/light/agri_x/light/set") == "agri_x"
    assert hub_compat.device_id_from_command_topic("homeassistant/switch/agri_x/power/set") == "agri_x"
    assert hub_compat.device_id_from_command_topic("esp32-bridge/force_update/set") is None


from fastapi.testclient import TestClient
from app.websocket_manager import WebSocketManager
from app.http_api import create_app


def _client(tmp_path):
    reg = DeviceRegistry(data_dir=str(tmp_path))
    reg.load()
    ws = WebSocketManager()
    app = create_app(reg, ws)
    app.state.mqtt = None
    return TestClient(app), reg


def test_node_endpoints(tmp_path):
    client, reg = _client(tmp_path)
    r = client.post("/node/register", json={"device_id": "agri_x", "sensor_type": 9, "device_name": "Lamp"})
    assert r.status_code == 200
    assert r.json()["assigned_slot"] == 0

    r = client.post("/node/state", json={"device_id": "agri_x", "relay_state": True})
    assert r.status_code == 200
    assert reg.get_device("agri_x").state["light"] is True

    r = client.post("/node/heartbeat", json={"device_id": "agri_x"})
    assert r.status_code == 200

    r = client.get("/node/command/agri_x")
    assert r.status_code == 200
    assert r.json() == {}

    reg.enqueue_hub_command("agri_x", "on")
    r = client.get("/node/command/agri_x")
    assert r.json() == {"command": "on", "slot": 0}

    r = client.post("/node/register", json={"device_id": "y", "sensor_type": 99})
    assert r.status_code == 400


import asyncio
import sys
import app.main as main_module


class _FakeMsg:
    def __init__(self, topic, payload):
        self.topic = topic
        self.payload = payload


class _FakeClient:
    def __init__(self, msgs):
        self._msgs = msgs
        self.subscribed = []

    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc):
        return False

    async def subscribe(self, topic):
        self.subscribed.append(topic)

    @property
    def messages(self):
        async def gen():
            for m in self._msgs:
                yield m
            raise RuntimeError("end of test stream")
        return gen()


class _FakeMQTT:
    def __init__(self, msgs):
        self._msgs = msgs

    def Client(self, **kwargs):
        return _FakeClient(self._msgs)


@pytest.mark.asyncio
async def test_hub_command_listener_enqueues(monkeypatch):
    fake = _FakeMQTT([_FakeMsg("homeassistant/light/agri_x/light/set", b"true")])
    monkeypatch.setitem(sys.modules, "aiomqtt", fake)
    calls = []
    monkeypatch.setattr(main_module.registry, "get_device", lambda d: d == "agri_x")
    monkeypatch.setattr(
        main_module.registry, "enqueue_hub_command",
        lambda d, c: calls.append((d, c)) or True,
    )
    task = asyncio.create_task(main_module.hub_command_listener())
    await asyncio.sleep(0.1)
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass
    assert calls == [("agri_x", "on")]
