# tests/test_integration_hub_compat.py
from unittest.mock import AsyncMock
from app.device_registry import DeviceRegistry
from app import hub_compat


async def _cycle(tmp_path, sensor_type, relay_key, relay_val, state_key, state_val):
    reg = DeviceRegistry(data_dir=str(tmp_path))
    reg.load()
    mqtt = AsyncMock()
    ws = AsyncMock()
    did = "agri_lamp1"
    resp, code = await hub_compat.handle_node_register(
        reg, mqtt, ws, {"device_id": did, "sensor_type": sensor_type, "device_name": "Lamp"}
    )
    assert code == 200
    assert resp["assigned_slot"] == 0
    resp, code = await hub_compat.handle_node_state(
        reg, ws, {"device_id": did, relay_key: relay_val, "ip": "192.168.1.20"}
    )
    assert code == 200
    dev = reg.get_device(did)
    assert dev.state[state_key] is state_val
    assert dev.ip == "192.168.1.20"
    # HA liga
    cmd = hub_compat.translate_ha_payload("true")
    assert cmd == "on"
    reg.enqueue_hub_command(did, cmd)
    resp, code = await hub_compat.handle_node_command_get(reg, did)
    assert resp == {"command": "on", "slot": 0}
    # consome e fica vazio
    resp, _ = await hub_compat.handle_node_command_get(reg, did)
    assert resp == {}


async def test_lamp_light_cycle(tmp_path):
    await _cycle(tmp_path, 9, "relay_state", True, "light", True)


async def test_onoff_switch_cycle(tmp_path):
    await _cycle(tmp_path, 8, "state", False, "power", False)