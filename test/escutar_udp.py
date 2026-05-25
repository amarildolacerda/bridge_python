#!/usr/bin/env python3
"""
UDP Bridge Simulator - Responde a descobertas de dispositivos
Simula um servidor bridge MQTT para testes
"""

import socket
import json
import threading
import time
import sys
from datetime import datetime

class UDPBridgeSimulator:
    def __init__(self, port=5000, mqtt_port=1883, http_port=8080, bridge_name="mqtt_bridge_simulator"):
        self.port = port
        self.mqtt_port = mqtt_port
        self.http_port = http_port
        self.bridge_name = bridge_name
        self.ip_sta = self.get_local_ip()
        self.sock = None
        self.running = False
        self.devices = {}  # Store discovered devices
        self.message_count = 0
        
    def get_local_ip(self):
        """Get local IP address"""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except:
            return "192.168.1.100"
    
    def start(self):
        """Start UDP bridge simulator"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            self.sock.bind(('', self.port))
            self.sock.settimeout(1)
            
            self.running = True
            
            print(f"🚀 MQTT Bridge Simulator started")
            print(f"📡 UDP Discovery Port: {self.port}")
            print(f"🔌 MQTT Port: {self.mqtt_port}")
            print(f"🌐 HTTP Port: {self.http_port}")
            print(f"💻 Bridge IP: {self.ip_sta}")
            print(f"🏷️  Bridge Name: {self.bridge_name}")
            print("=" * 70)
            
            # Start periodic broadcast thread
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
                    
                    print(f"\n📨 [{timestamp}] #{self.message_count} from {addr[0]}:{addr[1]}")
                    print(f"📝 Message: {message[:200]}")
                    
                    # Process different message types
                    self.process_message(message, addr)
                    
                except socket.timeout:
                    continue
                except Exception as e:
                    print(f"⚠️ Error: {e}")
                    
        except KeyboardInterrupt:
            print("\n\n⏹️ Stopping simulator...")
        finally:
            self.stop()
    
    def process_message(self, message, addr):
        """Process incoming UDP messages"""
        
        # Check for JSON messages
        try:
            data = json.loads(message)
            
            # Device registration
            if 'id' in data and 'type' in data:
                device_id = data.get('id')
                device_type = data.get('type')
                device_name = data.get('name', 'Unknown')
                
                # Store device
                self.devices[device_id] = {
                    'id': device_id,
                    'type': device_type,
                    'name': device_name,
                    'ip': addr[0],
                    'last_seen': time.time(),
                    'data': data
                }
                
                print(f"✅ Device registered: {device_id}")
                print(f"   Type: {device_type}")
                print(f"   Name: {device_name}")
                print(f"   IP: {addr[0]}")
                
                # Send acknowledgment
                response = {
                    "status": "registered",
                    "device_id": device_id,
                    "message": "Device registered successfully",
                    "timestamp": int(time.time())
                }
                self.send_response(addr, response)
                
            # Device state update
            elif 'device_id' in data and 'state' in data:
                device_id = data['device_id']
                print(f"📊 State update from {device_id}")
                if 'temperature' in data:
                    print(f"   Temperature: {data['temperature']}°C")
                if 'humidity' in data:
                    print(f"   Humidity: {data['humidity']}%")
                if 'state' in data:
                    print(f"   State: {data['state']}")
                if 'power' in data:
                    print(f"   Power: {data['power']}W")
                    
            # Device telemetry
            elif 'device_id' in data and 'rssi' in data:
                device_id = data['device_id']
                print(f"📡 Telemetry from {device_id}")
                print(f"   RSSI: {data.get('rssi', 'N/A')} dBm")
                print(f"   Uptime: {data.get('uptime', 'N/A')}s")
                print(f"   Free Heap: {data.get('free_heap', 'N/A')} bytes")
                
            # Device event
            elif 'device_id' in data and 'event' in data:
                device_id = data['device_id']
                print(f"⚡ Event from {device_id}")
                print(f"   Event: {data.get('event', 'N/A')}")
                print(f"   Severity: {data.get('severity', 'N/A')}")
                
            # ESP8266 device announcement
            elif data.get('type') == 'esp8266_device':
                device_id = data.get('device_id')
                print(f"🔌 ESP8266 device announced: {device_id}")
                print(f"   Name: {data.get('device_name', 'N/A')}")
                print(f"   Type: {data.get('device_type', 'N/A')}")
                print(f"   IP: {data.get('ip', 'N/A')}")
                print(f"   MAC: {data.get('mac', 'N/A')}")
                
                # Store device
                self.devices[device_id] = {
                    'id': device_id,
                    'type': data.get('device_type'),
                    'name': data.get('device_name'),
                    'ip': data.get('ip'),
                    'mac': data.get('mac'),
                    'last_seen': time.time()
                }
                
        except json.JSONDecodeError:
            # Not JSON, check for text commands
            if message == "MQTT_DISCOVERY_REQUEST":
                print(f"🔍 MQTT Discovery request received from {addr[0]}")
                self.send_bridge_info(addr)
                
            elif message == "ESP8266_DISCOVERY_REQUEST":
                print(f"🔍 ESP8266 Discovery request received from {addr[0]}")
                self.send_bridge_info(addr)
                
            elif message.startswith("BRIDGE_RESPONSE:"):
                print(f"📢 Legacy bridge response received")
                
            else:
                print(f"⚠️ Unknown message format")
    
    def send_response(self, addr, response_data):
        """Send JSON response to client"""
        try:
            response = json.dumps(response_data)
            self.sock.sendto(response.encode(), addr)
            print(f"📤 Sent response to {addr[0]}: {response_data.get('status', 'unknown')}")
        except Exception as e:
            print(f"❌ Failed to send response: {e}")
    
    def send_bridge_info(self, addr):
        """Send bridge information to requesting device"""
        response = {
            "service": "mqtt-bridge",
            "name": self.bridge_name,
            "mqtt_port": self.mqtt_port,
            "http_port": self.http_port,
            "ip_sta": self.ip_sta,
            "device_count": len(self.devices),
            "timestamp": int(time.time())
        }
        
        try:
            response_json = json.dumps(response)
            self.sock.sendto(response_json.encode(), addr)
            print(f"📤 Sent bridge info to {addr[0]}")
            print(f"   Response: {response_json}")
        except Exception as e:
            print(f"❌ Failed to send bridge info: {e}")
    
    def periodic_broadcast(self):
        """Periodically broadcast bridge presence"""
        while self.running:
            time.sleep(30)  # Broadcast every 30 seconds
            
            if self.running:
                broadcast_addr = ('255.255.255.255', self.port)
                response = {
                    "service": "mqtt-bridge",
                    "name": self.bridge_name,
                    "mqtt_port": self.mqtt_port,
                    "http_port": self.http_port,
                    "ip_sta": self.ip_sta,
                    "device_count": len(self.devices),
                    "timestamp": int(time.time())
                }
                
                try:
                    response_json = json.dumps(response)
                    self.sock.sendto(response_json.encode(), broadcast_addr)
                    print(f"\n📢 Broadcast sent to all devices")
                    print(f"   Devices known: {len(self.devices)}")
                except Exception as e:
                    print(f"❌ Broadcast failed: {e}")
    
    def stop(self):
        """Stop the simulator"""
        self.running = False
        if self.sock:
            self.sock.close()
        print(f"\n📊 Final statistics:")
        print(f"   Total messages received: {self.message_count}")
        print(f"   Devices discovered: {len(self.devices)}")
        if self.devices:
            print("\n   Device list:")
            for device_id, info in self.devices.items():
                print(f"     - {device_id}: {info.get('name', 'Unknown')} ({info.get('type', 'Unknown')})")
        print("\n✅ Simulator stopped")

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='UDP Bridge Simulator for MQTT Bridge Testing')
    parser.add_argument('--port', type=int, default=5000, help='UDP discovery port (default: 5000)')
    parser.add_argument('--mqtt-port', type=int, default=1883, help='MQTT broker port (default: 1883)')
    parser.add_argument('--http-port', type=int, default=8080, help='HTTP API port (default: 8080)')
    parser.add_argument('--name', type=str, default='mqtt_bridge_simulator', help='Bridge name')
    
    args = parser.parse_args()
    
    simulator = UDPBridgeSimulator(
        port=args.port,
        mqtt_port=args.mqtt_port,
        http_port=args.http_port,
        bridge_name=args.name
    )
    
    try:
        simulator.start()
    except KeyboardInterrupt:
        print("\n\n👋 Goodbye!")

if __name__ == "__main__":
    main()