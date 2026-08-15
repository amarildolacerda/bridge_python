from __future__ import annotations
import json
import struct
import pytest
from app.udp_discovery import build_announce_bytes, UDPDiscovery, DISCOVERY_SERVICE


class TestUDPDiscovery:
    @pytest.fixture
    def discovery(self):
        ud = UDPDiscovery(bridge_ip="192.168.1.50", http_port=80)
        yield ud

    def test_handle_discover_request(self, discovery):
        data = json.dumps({
            "service": DISCOVERY_SERVICE,
            "discover": True,
            "id": "esp8266_test",
        }).encode()
        discovery._handle_message(data, ("10.0.0.1", 5000))
        assert "esp8266_test" in discovery._discovered_ips
        assert discovery._discovered_ips["esp8266_test"][0] == "10.0.0.1"

    def test_handle_wrong_service(self, discovery):
        data = json.dumps({"service": "wrong"}).encode()
        discovery._handle_message(data, ("10.0.0.1", 5000))
        assert len(discovery._discovered_ips) == 0

    def test_prune_discovered(self, discovery):
        import time
        discovery._discovered_ips["old"] = ("10.0.0.1", time.time() - 600)
        discovery._discovered_ips["new"] = ("10.0.0.2", time.time())
        discovery._prune_discovered()
        assert "old" not in discovery._discovered_ips
        assert "new" in discovery._discovered_ips

    def test_do_broadcast_returns_dict(self, discovery):
        result = discovery.do_broadcast()
        assert "registered" in result
        assert "discovered" in result


def test_build_announce_bytes():
    b = build_announce_bytes("192.168.1.50", 80)
    assert len(b) == 23
    msg_type, fw, ip, port = struct.unpack("<B4s16sH", b)
    assert msg_type == 0x09
    assert port == 80
    assert ip.rstrip(b"\x00").decode() == "192.168.1.50"


def test_handle_binary_discover_sends_announce():
    udp = UDPDiscovery(bridge_ip="192.168.1.50", http_port=80)
    sent = []

    class FakeSock:
        def sendto(self, data, addr):
            sent.append((data, addr))
            return len(data)

    udp._sock = FakeSock()
    # tcp_gw_discover_t: msg_type=0x0A, sensor_type=9, device_name[32]
    discover = bytes([0x0A, 9]) + b"\x00" * 32
    udp._handle_message(discover, ("10.0.0.5", 1234))
    assert len(sent) == 1
    data, addr = sent[0]
    assert addr == ("10.0.0.5", 1234)
    assert data[0] == 0x09
