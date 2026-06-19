from __future__ import annotations
import json
import time
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from app.device_registry import DeviceRegistry
from app.models import DeviceType


def create_app(registry: DeviceRegistry) -> FastAPI:
    app = FastAPI(title="ESP32 Bridge Python", version="0.1.0")

    @app.get("/api/ping")
    async def ping():
        return {"status": "ok"}

    @app.post("/api/device/register")
    async def register_device(request: Request):
        try:
            body = await request.json()
        except json.JSONDecodeError:
            return JSONResponse({"status": "error", "message": "invalid json"}, status_code=400)
        device_id = body.get("id")
        device_type_str = body.get("type")
        if not device_id or not device_type_str:
            return JSONResponse({"status": "error", "message": "missing id or type"}, status_code=400)
        device_type = DeviceType.from_string(device_type_str)
        if device_type == DeviceType.UNKNOWN:
            return JSONResponse({"status": "error", "message": "unknown type"}, status_code=400)
        name = body.get("name", device_id)
        ip = body.get("ip", "")
        slot = registry.register(device_id, device_type, name, ip)
        if slot == -1:
            return JSONResponse({"status": "error", "message": "registry full"}, status_code=500)
        return {"status": "ok", "slot": slot}

    @app.post("/api/device/remove")
    async def remove_device(request: Request):
        try:
            body = await request.json()
        except json.JSONDecodeError:
            return JSONResponse({"status": "error", "message": "invalid json"}, status_code=400)
        device_id = body.get("id")
        if not device_id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        if registry.remove(device_id):
            return {"status": "ok"}
        return JSONResponse({"status": "error", "message": "device not found"}, status_code=404)

    @app.post("/api/device/state")
    async def device_state(request: Request):
        try:
            body = await request.json()
        except json.JSONDecodeError:
            return JSONResponse({"status": "error", "message": "invalid json"}, status_code=400)
        device_id = body.get("id")
        if not device_id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        found = False
        for key, value in body.items():
            if key == "id":
                continue
            if registry.update_state(device_id, key, value):
                found = True
        if not found:
            return JSONResponse({"status": "error", "message": "device not found"}, status_code=404)
        return {"status": "ok"}

    @app.get("/api/device/commands")
    async def device_commands_get(id: str = ""):
        if not id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        cmds = registry.get_commands(id)
        return {"commands": cmds}

    @app.post("/api/device/commands")
    async def device_commands_post(request: Request):
        try:
            body = await request.json()
        except json.JSONDecodeError:
            return JSONResponse({"status": "error", "message": "invalid json"}, status_code=400)
        device_id = body.get("id")
        if not device_id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        cmds = registry.get_commands(device_id)
        return {"commands": cmds}

    @app.get("/api/device/info")
    async def device_info(id: str = ""):
        if not id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        dev = registry.get_device(id)
        if not dev:
            return JSONResponse({"status": "error", "message": "device not found"}, status_code=404)
        return {
            "id": dev.id,
            "name": dev.name,
            "type": dev.type.value,
            "ip": dev.ip,
            "online": dev.online,
            "last_seen": dev.last_seen,
            "state": dev.state,
        }

    @app.get("/api/devices")
    async def devices_list():
        return [
            {
                "id": d.id,
                "name": d.name,
                "type": d.type.value,
                "ip": d.ip,
                "online": d.online,
                "state": d.state,
                "last_seen": d.last_seen,
            }
            for d in registry.get_all()
        ]

    @app.get("/api/gateway/info")
    async def gateway_info():
        import socket
        hostname = socket.gethostname()
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
        except Exception:
            ip = "127.0.0.1"
        return {
            "ip": ip,
            "gateway": "",
            "netmask": "",
            "dns1": "",
            "dns2": "",
            "version": "v0.1.0",
            "uptime_s": int(time.time()),
            "heap_free": 0,
            "total_devices": len(registry.get_all()),
            "hostname": hostname,
        }

    @app.post("/api/device/heartbeat")
    async def device_heartbeat(request: Request):
        try:
            body = await request.json()
        except json.JSONDecodeError:
            return JSONResponse({"status": "error", "message": "invalid json"}, status_code=400)
        device_id = body.get("id")
        if not device_id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        dev = registry.get_device(device_id)
        if not dev:
            return JSONResponse({"status": "error", "message": "device not found"}, status_code=404)
        dev.last_seen = time.time()
        dev.online = True
        return {"status": "ok"}

    @app.post("/api/gateway/broadcast")
    async def broadcast():
        return {"status": "ok", "message": "broadcast sent"}

    @app.post("/api/gateway/reset")
    async def reset():
        return {"status": "ok", "message": "reset initiated"}

    @app.get("/api/qrcode")
    async def qrcode():
        return {"service_name": "esp-bridge", "pop": ""}

    @app.post("/api/ota")
    async def ota():
        return {"status": "ok", "message": "ota not applicable in python"}

    return app
