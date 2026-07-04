from __future__ import annotations
import asyncio
import logging
import socket
from zeroconf import Zeroconf, ServiceInfo

LOG = logging.getLogger(__name__)

SERVICE_TYPE = "_esp-bridge._tcp.local."
SERVICE_NAME = "ESP Bridge"

class MDNSDiscovery:
    def __init__(self, http_port: int = 80, bridge_ip: str = ""):
        self._http_port = http_port
        self._bridge_ip = bridge_ip
        self._zeroconf: Zeroconf | None = None
        self._service_info: ServiceInfo | None = None

    async def start(self):
        loop = asyncio.get_event_loop()
        self._zeroconf = await loop.run_in_executor(None, Zeroconf)
        ip = self._bridge_ip or _get_local_ip()
        ips = socket.gethostbyname_ex(socket.gethostname())[2] if not ip else [ip]
        server = socket.gethostname()

        self._service_info = ServiceInfo(
            SERVICE_TYPE,
            f"{SERVICE_NAME}.{SERVICE_TYPE}",
            addresses=[socket.inet_aton(addr) for addr in ips],
            port=self._http_port,
            properties={"path": "/"},
            server=f"{server}.local.",
        )
        await loop.run_in_executor(None, self._zeroconf.register_service, self._service_info)
        LOG.info("mDNS service registered: %s (%s:%d)", SERVICE_NAME, ip, self._http_port)

    async def stop(self):
        if self._zeroconf and self._service_info:
            loop = asyncio.get_event_loop()
            await loop.run_in_executor(None, self._zeroconf.unregister_service, self._service_info)
            await loop.run_in_executor(None, self._zeroconf.close)
            LOG.info("mDNS service unregistered")


def _get_local_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(1)
        s.connect(("8.8.8.8", 53))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "0.0.0.0"
