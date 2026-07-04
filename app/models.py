from __future__ import annotations
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Optional
from pydantic import BaseModel


class DeviceType(str, Enum):
    ONOFF = "onoff"
    DIMMABLE = "dimmable"
    TEMPERATURE = "temperature"
    HUMIDITY = "humidity"
    CONTACT = "contact"
    OCCUPANCY = "occupancy"
    LIGHT_SENSOR = "light_sensor"
    TANQUE = "tanque"
    GAS = "gas"
    DHT_GAS = "dht_gas"
    RAIN = "rain"
    ELECTRICITY = "electricity"
    BRIDGE = "bridge"
    UNKNOWN = "unknown"

    @classmethod
    def from_string(cls, s: str) -> DeviceType:
        try:
            return cls(s.lower())
        except ValueError:
            return cls.UNKNOWN


@dataclass
class BridgedDevice:
    id: str
    name: str
    type: DeviceType
    ip: str = ""
    registered: bool = True
    online: bool = False
    last_seen: float = 0.0
    state: dict[str, float | bool | str] = field(default_factory=dict)
    commands: list[dict] = field(default_factory=list)


# ── Pydantic models para Swagger ──

class StatusResponse(BaseModel):
    status: str

class ErrorResponse(BaseModel):
    status: str
    message: str

class DeviceRegisterRequest(BaseModel):
    id: str
    type: str
    name: str = ""
    ip: str = ""

class DeviceRemoveRequest(BaseModel):
    id: str

class DeviceStateRequest(BaseModel):
    id: str

class DeviceHeartbeatRequest(BaseModel):
    id: str

class DeviceCommandItem(BaseModel):
    cluster: str
    command: str
    data: str

class DeviceCommandsRequest(BaseModel):
    id: str
    commands: list[DeviceCommandItem] | None = None

class DeviceCommandsResponse(BaseModel):
    commands: list[dict]

class DeviceInfoResponse(BaseModel):
    id: str
    name: str
    type: str
    ip: str
    online: bool
    last_seen: float
    state: dict[str, Any]

class DeviceListItem(BaseModel):
    id: str
    name: str
    type: str
    ip: str
    online: bool
    state: dict[str, Any]
    last_seen: float

class GatewayInfoResponse(BaseModel):
    ip: str
    version: str
    uptime_s: int
    total_devices: int
    hostname: str

class BroadcastResponse(BaseModel):
    status: str
    message: str

class ResetResponse(BaseModel):
    status: str
    message: str

class OtaResponse(BaseModel):
    status: str
    message: str

class QRCodeResponse(BaseModel):
    service_name: str
    pop: str

class PingResponse(BaseModel):
    status: str

class StatusOkResponse(BaseModel):
    status: str
    slot: int | None = None

