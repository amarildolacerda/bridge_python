from __future__ import annotations
import asyncio
import logging
import os
import time
from app.config import settings
from app.device_registry import DeviceRegistry
from app.http_api import create_app
from app.host_monitor import HostMonitor
from app.mqtt_discovery import MQTTDiscovery
from app.udp_discovery import UDPDiscovery
from app.mdns_discovery import MDNSDiscovery
from app.websocket_manager import WebSocketManager

LOG = logging.getLogger(__name__)

registry = DeviceRegistry(data_dir=settings.data_dir)
ws_manager = WebSocketManager()
udp = UDPDiscovery(bridge_ip=settings.bridge_ip, http_port=settings.http_port)
mdns = MDNSDiscovery(http_port=settings.http_port, bridge_ip=settings.bridge_ip)
app = create_app(registry, ws_manager, udp)
app.state.ws_manager = ws_manager

mqtt = MQTTDiscovery(
    host=settings.mqtt_host,
    port=settings.mqtt_port,
    user=settings.mqtt_user,
    password=settings.mqtt_pass,
)
app.state.mqtt = mqtt
host_monitor = HostMonitor(mqtt)


async def heartbeat_monitor():
    while True:
        await asyncio.sleep(30)
        before = {d.id: d.online for d in registry.get_all()}
        registry.check_heartbeats(timeout=60)
        for dev in registry.get_all():
            if before.get(dev.id, False) != dev.online:
                await ws_manager.notify_device_online(dev.id, dev.online)


async def mqtt_state_sync():
    while True:
        await asyncio.sleep(5)
        for dev in registry.get_all():
            if dev.online:
                await mqtt.publish_state(dev)


async def handle_force_update():
    from app.git_pull import git_pull as do_git_pull
    result = await do_git_pull()
    success = result["success"]
    message = result["message"]
    updated = result.get("updated", False)
    LOG.info("Force update result: success=%s updated=%s message=%s", success, updated, message)
    await mqtt.publish_force_update_result(success, message)
    if success and updated:
        LOG.info("Code updated, restarting in 1s...")
        await asyncio.sleep(1)
        os._exit(0)


async def force_update_listener():
    import aiomqtt
    while True:
        try:
            async with aiomqtt.Client(
                hostname=settings.mqtt_host,
                port=settings.mqtt_port,
                username=settings.mqtt_user or None,
                password=settings.mqtt_pass or None,
            ) as client:
                messages = client.messages
                await client.subscribe("esp32-bridge/force_update/set")
                async for message in messages:
                    payload = message.payload.decode()
                    if payload == "PRESS":
                        LOG.info("Force update triggered via MQTT")
                        asyncio.create_task(handle_force_update())
        except Exception:
            LOG.exception("Force update listener error, retrying in 10s")
            await asyncio.sleep(10)


async def startup():
    registry.load()
    LOG.info("Loaded %d devices from persistence", len(registry.get_all()))
    for dev in registry.get_all():
        await mqtt.publish_device_config(dev)
    await mqtt.publish_force_update_config()
    await udp.start()
    await mdns.start()
    await host_monitor.start()
    asyncio.create_task(heartbeat_monitor())
    asyncio.create_task(mqtt_state_sync())
    asyncio.create_task(force_update_listener())


@app.on_event("startup")
async def on_startup():
    await mqtt.start()
    await startup()

@app.on_event("shutdown")
async def on_shutdown():
    await mqtt.remove_force_update_config()


def main() -> None:
    import uvicorn
    logging.basicConfig(
        level=getattr(logging, settings.log_level.upper(), logging.INFO),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    uvicorn.run(app, host="0.0.0.0", port=settings.http_port, log_level=settings.log_level)


if __name__ == "__main__":
    main()
