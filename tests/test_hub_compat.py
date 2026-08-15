from app.models import DeviceType
from app.mqtt_discovery import DEVICE_ENTITY_MAP


def test_light_device_type_exists():
    assert DeviceType.LIGHT.value == "light"


def test_light_entity_map_entry():
    assert DEVICE_ENTITY_MAP[DeviceType.LIGHT] == [("light", "light", "", "", "")]
