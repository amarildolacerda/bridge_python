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
