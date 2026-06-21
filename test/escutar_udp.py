#!/usr/bin/env python3
"""
UDP Bridge Simulator - Responde a descobertas de dispositivos
Simula um servidor bridge HTTP REST para testes
"""

import socket
import json
import threading
import time
import sys
from datetime import datetime

class UDPBridgeSimulator:
    def __init__(self, port=5000, http_port=80, bridge_name="esp_bridge_simulator"):
        self.port = port
        self.http_port = http_port
        self.bridge_name = bridge_name
        self.ip_sta = self.get_local_ip()
        self.sock = None
        self.running = False
        self.devices = {}
        self.message_count = 0

    def get_local_ip(self):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except:
            return "192.168.1.100"

    def start(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            self.sock.bind(('', self.port))
            self.sock.settimeout(1)

            self.running = True

            print(f"Bridge Simulator started")
            print(f"UDP Discovery Port: {self.port}")
            print(f"HTTP Port: {self.http_port}")
            print(f"Bridge IP: {self.ip_sta}")
            print(f"Bridge Name: {self.bridge_name}")
            print("=" * 70)

            broadcast_thread = threading.Thread(target=self.periodic_broadcast, daemon=True)
            broadcast_thread.start()

            while self.running:
                try:
                    data, addr = self.sock.recvfrom(4096)
                    self.message_count += 1
                    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]

                    try:
                        message = data.decode('utf-8')
                    except:
                        message = str(data)

                    print(f"\n[{timestamp}] #{self.message_count} from {addr[0]}:{addr[1]}")
                    print(f"Message: {message[:200]}")

                    self.process_message(message, addr)

                except socket.timeout:
                    continue
                except Exception as e:
                    print(f"Error: {e}")

        except KeyboardInterrupt:
            print("\n\nStopping simulator...")
        finally:
            self.stop()

    def process_message(self, message, addr):
        try:
            data = json.loads(message)

            if data.get("discover") or data.get("service") == "esp-bridge":
                print(f"Discovery request from {addr[0]}")
                self.send_bridge_info(addr)

        except json.JSONDecodeError:
            print(f"Unknown message format")

    def send_bridge_info(self, addr):
        response = {
            "service": "esp-bridge",
            "name": self.bridge_name,
            "http_port": self.http_port,
            "ip_sta": self.ip_sta,
            "device_count": len(self.devices),
            "timestamp": int(time.time())
        }

        try:
            response_json = json.dumps(response)
            self.sock.sendto(response_json.encode(), addr)
            print(f"Sent bridge info to {addr[0]}: {response_json}")
        except Exception as e:
            print(f"Failed to send bridge info: {e}")

    def periodic_broadcast(self):
        while self.running:
            time.sleep(30)

            if self.running:
                broadcast_addr = ('255.255.255.255', self.port)
                response = {
                    "service": "esp-bridge",
                    "name": self.bridge_name,
                    "http_port": self.http_port,
                    "ip_sta": self.ip_sta,
                    "device_count": len(self.devices),
                    "timestamp": int(time.time())
                }

                try:
                    response_json = json.dumps(response)
                    self.sock.sendto(response_json.encode(), broadcast_addr)
                    print(f"\nBroadcast sent to all devices")
                    print(f"Devices known: {len(self.devices)}")
                except Exception as e:
                    print(f"Broadcast failed: {e}")

    def stop(self):
        self.running = False
        if self.sock:
            self.sock.close()
        print(f"\nFinal statistics:")
        print(f"Messages received: {self.message_count}")
        if self.devices:
            print("\nDevice list:")
            for device_id, info in self.devices.items():
                print(f"  - {device_id}: {info.get('name', 'Unknown')} ({info.get('type', 'Unknown')})")
        print("Simulator stopped")

def main():
    import argparse

    parser = argparse.ArgumentParser(description='UDP Bridge Simulator for HTTP REST Bridge Testing')
    parser.add_argument('--port', type=int, default=5000, help='UDP discovery port (default: 5000)')
    parser.add_argument('--http-port', type=int, default=80, help='HTTP API port (default: 80)')
    parser.add_argument('--name', type=str, default='esp_bridge_simulator', help='Bridge name')

    args = parser.parse_args()

    simulator = UDPBridgeSimulator(
        port=args.port,
        http_port=args.http_port,
        bridge_name=args.name
    )

    try:
        simulator.start()
    except KeyboardInterrupt:
        print("\n\nGoodbye!")

if __name__ == "__main__":
    main()
