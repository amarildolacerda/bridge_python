from __future__ import annotations
import asyncio
import json
import logging
import os
import time
from typing import Any
from fastapi import FastAPI, Query, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse, Response
from app.device_registry import DeviceRegistry
from app.models import (
    DeviceType,
    BroadcastResponse,
    DeviceCommandsRequest,
    DeviceCommandsResponse,
    DeviceHeartbeatRequest,
    DeviceInfoResponse,
    DeviceListItem,
    DeviceRegisterRequest,
    DeviceRemoveRequest,
    ErrorResponse,
    GatewayInfoResponse,
    OtaResponse,
    PingResponse,
    QRCodeResponse,
    ResetResponse,
    StatusOkResponse,
    StatusResponse,
)
from app.udp_discovery import UDPDiscovery
from app.websocket_manager import WebSocketManager

LOG = logging.getLogger(__name__)

_start_time = time.monotonic()

_tags = [
    {"name": "devices", "description": "Gerenciamento de devices"},
    {"name": "gateway", "description": "Informações e controle do gateway"},
    {"name": "dashboard", "description": "Dashboard web"},
]


def _uptime_s() -> int:
    return int(time.monotonic() - _start_time)


def create_app(registry: DeviceRegistry, ws_manager: WebSocketManager | None = None, udp_discovery: UDPDiscovery | None = None) -> FastAPI:
    if ws_manager is None:
        ws_manager = WebSocketManager()
    app = FastAPI(title="Home Bridge", version="v0.0.14", openapi_tags=_tags)
    app.state.ws_manager = ws_manager
    if udp_discovery:
        app.state.udp_discovery = udp_discovery

    @app.get("/api/ping", response_model=PingResponse, tags=["gateway"], summary="Health check")
    async def ping():
        return {"status": "ok"}

    @app.post("/api/device/register", response_model=StatusOkResponse | ErrorResponse, tags=["devices"], summary="Registrar um novo device")
    async def register_device(body: DeviceRegisterRequest, request: Request):
        device_type = DeviceType.from_string(body.type)
        if device_type == DeviceType.UNKNOWN:
            return JSONResponse({"status": "error", "message": "unknown type"}, status_code=400)
        name = body.name or body.id
        slot = registry.register(body.id, device_type, name, body.ip)
        if slot == -1:
            LOG.warning("Registro recusado: %s (limite de %d devices)", body.id, 32)
            return JSONResponse({"status": "error", "message": "registry full"}, status_code=500)
        LOG.info("Device registrado: %s (%s) como %s em %s", body.id, name, device_type.value, body.ip or "?")
        ws_manager = request.app.state.ws_manager
        await ws_manager.notify_device_registered(registry.get_device(body.id))
        mqtt = getattr(request.app.state, "mqtt", None)
        if mqtt:
            await mqtt.publish_device_config(registry.get_device(body.id))
        return {"status": "ok", "slot": slot}

    @app.post("/api/device/remove", response_model=StatusResponse | ErrorResponse, tags=["devices"], summary="Remover um device registrado")
    async def remove_device(body: DeviceRemoveRequest, request: Request):
        if registry.remove(body.id):
            LOG.info("Device removido: %s", body.id)
            ws_manager = request.app.state.ws_manager
            await ws_manager.notify_device_removed(body.id)
            return {"status": "ok"}
        LOG.warning("Remocao falhou: device %s nao encontrado", body.id)
        return JSONResponse({"status": "error", "message": "device not found"}, status_code=404)

    @app.post("/api/device/state", response_model=StatusResponse | ErrorResponse, tags=["devices"], summary="Atualizar estado de um device")
    async def device_state(body: dict[str, Any], request: Request):
        device_id = body.get("id")
        if not device_id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        found = False
        state_items = {}
        for key, value in body.items():
            if key == "id":
                continue
            state_items[key] = value
            if registry.update_state(device_id, key, value):
                found = True
        if not found:
            LOG.warning("State update falhou: device %s nao encontrado", device_id)
            return JSONResponse({"status": "error", "message": "device not found"}, status_code=404)
        LOG.info("State recebido de %s: %s", device_id, state_items)
        ws_manager = request.app.state.ws_manager
        dev = registry.get_device(device_id)
        if dev:
            await ws_manager.notify_device_update(dev)
        return {"status": "ok"}

    @app.get("/api/device/commands", response_model=DeviceCommandsResponse | ErrorResponse, tags=["devices"], summary="Obter comandos pendentes de um device")
    async def device_commands_get(id: str = Query("", description="ID do device")):
        if not id:
            return JSONResponse({"status": "error", "message": "missing id"}, status_code=400)
        cmds = registry.get_commands(id)
        if cmds:
            LOG.info("Comandos enviados para %s: %s", id, cmds)
        return {"commands": cmds}

    @app.post("/api/device/commands", response_model=DeviceCommandsResponse | dict, tags=["devices"], summary="Adicionar ou obter comandos de um device")
    async def device_commands_post(body: DeviceCommandsRequest):
        if body.commands:
            added = 0
            for cmd in body.commands:
                if registry.add_command(body.id, cmd.cluster, cmd.command, cmd.data):
                    added += 1
            if added:
                LOG.info("Comandos adicionados para %s: %d", body.id, added)
            return {"status": True, "added": added}

        cmds = registry.get_commands(body.id)
        if cmds:
            LOG.info("Comandos enviados para %s: %s", body.id, cmds)
        return {"commands": cmds}

    @app.get("/api/device/info", response_model=DeviceInfoResponse | ErrorResponse, tags=["devices"], summary="Obter detalhes de um device")
    async def device_info(id: str = Query("", description="ID do device")):
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

    @app.get("/api/devices", response_model=list[DeviceListItem], tags=["devices"], summary="Listar todos os devices registrados")
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

    @app.get("/api/gateway/info", response_model=GatewayInfoResponse, tags=["gateway"], summary="Informações do gateway")
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
            "version": "v0.0.14",
            "uptime_s": _uptime_s(),
            "total_devices": len(registry.get_all()),
            "hostname": hostname,
        }

    @app.post("/api/device/heartbeat", response_model=StatusResponse | ErrorResponse, tags=["devices"], summary="Receber heartbeat de um device")
    async def device_heartbeat(body: DeviceHeartbeatRequest, request: Request):
        dev = registry.get_device(body.id)
        if not dev:
            LOG.warning("Heartbeat de device desconhecido: %s", body.id)
            return JSONResponse({"status": "error", "message": "device not found"}, status_code=404)
        dev.last_seen = time.time()
        dev.online = True
        ws_manager = request.app.state.ws_manager
        await ws_manager.notify_device_online(body.id, True)
        LOG.info("Heartbeat recebido de %s (%s)", body.id, dev.name)
        return {"status": "ok"}

    @app.post("/api/gateway/broadcast", response_model=BroadcastResponse, tags=["gateway"], summary="Enviar broadcast UDP para re-registro de devices")
    async def broadcast(request: Request):
        udp = getattr(request.app.state, "udp_discovery", None)
        if udp:
            udp.do_broadcast()
            LOG.info("Broadcast enviado")
        return {"status": "ok", "message": "broadcast sent"}

    @app.post("/api/gateway/reset", response_model=ResetResponse, tags=["gateway"], summary="Reiniciar o gateway")
    async def reset():
        loop = asyncio.get_event_loop()
        loop.call_later(0.5, os._exit, 0)
        return {"status": "ok", "message": "reset initiated"}

    @app.post("/api/gateway/git-pull", tags=["gateway"], summary="Forçar git pull e reiniciar")
    async def git_pull_endpoint():
        from app.git_pull import git_pull as do_git_pull
        result = await do_git_pull()
        return JSONResponse(result, status_code=200 if result["success"] else 500)

    @app.get("/api/qrcode", response_model=QRCodeResponse, tags=["gateway"], summary="Obter dados do QR code RainMaker")
    async def qrcode():
        return {"service_name": "esp-bridge", "pop": ""}

    @app.post("/api/ota", response_model=OtaResponse, tags=["gateway"], summary="OTA (não aplicável no Python)")
    async def ota():
        return {"status": "ok", "message": "ota not applicable in python"}

    @app.websocket("/ws")
    async def ws_endpoint(websocket: WebSocket):
        await ws_manager.connect(websocket)
        try:
            while True:
                await websocket.receive_text()
        except WebSocketDisconnect:
            ws_manager.disconnect(websocket)

    @app.get("/", response_class=HTMLResponse, tags=["dashboard"], summary="Dashboard web", include_in_schema=False)
    async def dashboard_html():
        from app.web import dashboard_html_content
        return HTMLResponse(content=dashboard_html_content)

    @app.get("/dashboard.css", include_in_schema=False)
    async def dashboard_css():
        from app.web import dashboard_css_content
        return Response(content=dashboard_css_content, media_type="text/css")

    return app
