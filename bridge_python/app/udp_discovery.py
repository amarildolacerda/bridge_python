from __future__ import annotations
import asyncio
import json
import logging
import socket
import time

LOG = logging.getLogger(__name__)

DISCOVERY_PORT = 5000
DISCOVERY_SERVICE = "esp-bridge"
BROADCAST_INTERVAL = 300
BROADCAST_DELAY = 30
MAX_DISCOVERED_IPS = 8
DISCOVERED_IP_TIMEOUT = 300


class UDPDiscovery:
    def __init__(self, bridge_ip: str = "0.0.0.0", http_port: int = 80):
        self._bridge_ip = bridge_ip
        self._http_port = http_port
        self._discovered_ips: dict[str, tuple[str, float]] = {}
        self._running = False
        self._start_time = 0.0

    async def start(self):
        self._running = True
        self._start_time = time.time()
        loop = asyncio.get_event_loop()
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.setblocking(False)
        self._sock.bind(("0.0.0.0", DISCOVERY_PORT))
        LOG.info("UDP discovery listening on port %d", DISCOVERY_PORT)
        self._listener_task = asyncio.create_task(self._listen_loop())
        self._broadcast_task = asyncio.create_task(self._broadcast_loop())

    async def stop(self):
        self._running = False
        self._listener_task.cancel()
        self._broadcast_task.cancel()
        self._sock.close()

    async def _listen_loop(self):
        loop = asyncio.get_event_loop()
        while self._running:
            try:
                data, addr = await loop.sock_recvfrom(self._sock, 1024)
                self._handle_message(data, addr)
            except asyncio.CancelledError:
                break
            except Exception:
                LOG.exception("UDP recv error")

    def _handle_message(self, data: bytes, addr: tuple[str, int]):
        try:
            msg = json.loads(data.decode())
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        service = msg.get("service")
        if service != DISCOVERY_SERVICE:
            return
        if msg.get("discover") is True:
            sender_id = msg.get("id", addr[0])
            self._discovered_ips[sender_id] = (addr[0], time.time())
            self._prune_discovered()
            self._send_response(addr[0])

    def _send_response(self, target_ip: str):
        msg = json.dumps({
            "service": DISCOVERY_SERVICE,
            "ip_sta": self._bridge_ip,
            "http_port": self._http_port,
        }).encode()
        try:
            self._sock.sendto(msg, (target_ip, DISCOVERY_PORT))
        except Exception:
            LOG.exception("UDP send error")

    async def _broadcast_loop(self):
        await asyncio.sleep(BROADCAST_DELAY)
        while self._running:
            self._send_broadcast()
            await asyncio.sleep(BROADCAST_INTERVAL)

    def _send_broadcast(self):
        uptime = int(time.time() - self._start_time)
        msg = json.dumps({
            "service": DISCOVERY_SERVICE,
            "ip_sta": self._bridge_ip,
            "http_port": self._http_port,
            "uptime_s": uptime,
            "re_register": True,
        }).encode()
        try:
            self._sock.sendto(msg, ("255.255.255.255", DISCOVERY_PORT))
        except Exception:
            LOG.exception("UDP broadcast error")

    def do_broadcast(self) -> dict:
        self._send_broadcast()
        return {
            "registered": [],
            "discovered": [
                {"id": did, "ip": ip}
                for did, (ip, _) in self._discovered_ips.items()
            ],
        }

    def get_bridge_ip(self) -> str:
        return self._bridge_ip

    def _prune_discovered(self):
        now = time.time()
        to_remove = [
            did for did, (_, ts) in self._discovered_ips.items()
            if now - ts > DISCOVERED_IP_TIMEOUT
        ]
        for did in to_remove:
            del self._discovered_ips[did]
