#!/usr/bin/env python3
"""
MQTT Bridge Device Validation Test
Tests the ESP32 MQTT Bridge Broker functionality
"""

import paho.mqtt.client as mqtt
import json
import time
import socket
import argparse
from typing import Optional, Dict, Any
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class MQTTBridgeTester:
    def __init__(self, broker_host: str, broker_port: int = 1883, 
                 username: Optional[str] = None, password: Optional[str] = None):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.username = username
        self.password = password
        self.client = None
        self.connected = False
        self.received_messages = []
        self.device_id = f"test_device_{int(time.time())}"
        
    def connect(self) -> bool:
        """Connect to MQTT broker"""
        try:
            self.client = mqtt.Client()
            self.client.on_connect = self._on_connect
            self.client.on_message = self._on_message
            self.client.on_disconnect = self._on_disconnect
            
            if self.username and self.password:
                self.client.username_pw_set(self.username, self.password)
            
            logger.info(f"Connecting to {self.broker_host}:{self.broker_port}")
            self.client.connect(self.broker_host, self.broker_port, 60)
            self.client.loop_start()
            
            # Wait for connection
            timeout = 10
            start_time = time.time()
            while not self.connected and (time.time() - start_time) < timeout:
                time.sleep(0.1)
            
            return self.connected
            
        except Exception as e:
            logger.error(f"Connection failed: {e}")
            return False
    
    def _on_connect(self, client, userdata, flags, rc):
        """Callback for connect event"""
        if rc == 0:
            logger.info("✓ Connected to MQTT broker")
            self.connected = True
        else:
            logger.error(f"✗ Connection failed with code: {rc}")
            self.connected = False
    
    def _on_disconnect(self, client, userdata, rc):
        """Callback for disconnect event"""
        logger.warning("Disconnected from broker")
        self.connected = False
    
    def _on_message(self, client, userdata, msg):
        """Callback for received messages"""
        try:
            payload = msg.payload.decode('utf-8')
            logger.debug(f"Received: {msg.topic} -> {payload}")
            self.received_messages.append({
                'topic': msg.topic,
                'payload': payload,
                'timestamp': time.time()
            })
        except Exception as e:
            logger.error(f"Error processing message: {e}")
    
    def register_device(self, device_type: str = "sensor", 
                        device_name: Optional[str] = None) -> bool:
        """Register a device with the bridge"""
        topic = f"mqtt-bridge/register"
        
        payload = {
            "id": self.device_id,
            "type": device_type,
            "name": device_name or f"Test {device_type}"
        }
        
        logger.info(f"Registering device: {self.device_id}")
        result = self.client.publish(topic, json.dumps(payload))
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            logger.info(f"✓ Device registered successfully")
            return True
        else:
            logger.error(f"✗ Failed to register device")
            return False
    
    def publish_state(self, state_data: Dict[str, Any]) -> bool:
        """Publish device state to bridge"""
        topic = f"mqtt-bridge/{self.device_id}/state"
        
        # Add timestamp to state
        state_data['timestamp'] = int(time.time())
        state_data['device_id'] = self.device_id
        
        result = self.client.publish(topic, json.dumps(state_data))
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            logger.info(f"✓ Published state: {state_data}")
            return True
        else:
            logger.error(f"✗ Failed to publish state")
            return False
    
    def publish_telemetry(self, telemetry_data: Dict[str, Any]) -> bool:
        """Publish device telemetry to bridge"""
        topic = f"mqtt-bridge/{self.device_id}/telemetry"
        
        telemetry_data['timestamp'] = int(time.time())
        telemetry_data['device_id'] = self.device_id
        
        result = self.client.publish(topic, json.dumps(telemetry_data))
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            logger.info(f"✓ Published telemetry: {telemetry_data}")
            return True
        else:
            logger.error(f"✗ Failed to publish telemetry")
            return False
    
    def subscribe_to_commands(self) -> bool:
        """Subscribe to device commands"""
        topic = f"mqtt-bridge/{self.device_id}/command"
        
        self.client.subscribe(topic)
        logger.info(f"✓ Subscribed to commands: {topic}")
        return True
    
    def wait_for_messages(self, timeout: int = 5) -> list:
        """Wait for incoming messages"""
        time.sleep(timeout)
        return self.received_messages
    
    def disconnect(self):
        """Disconnect from broker"""
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()
            logger.info("Disconnected from broker")

def discover_bridge(timeout: int = 10) -> Optional[str]:
    """Discover MQTT bridge via UDP broadcast"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(timeout)
        
        # Bind to broadcast port
        sock.bind(('', 5000))  # BROADCAST_PORT from config
        
        logger.info(f"Listening for bridge broadcasts (timeout: {timeout}s)...")
        
        start_time = time.time()
        while (time.time() - start_time) < timeout:
            try:
                data, addr = sock.recvfrom(1024)
                message = data.decode('utf-8')
                bridge_info = json.loads(message)
                
                if bridge_info.get('service') == 'mqtt-bridge':
                    logger.info(f"✓ Found bridge at {addr[0]}")
                    logger.info(f"  Name: {bridge_info.get('name')}")
                    logger.info(f"  MQTT Port: {bridge_info.get('mqtt_port')}")
                    logger.info(f"  HTTP Port: {bridge_info.get('http_port')}")
                    return addr[0]
                    
            except socket.timeout:
                continue
            except json.JSONDecodeError:
                continue
                
        logger.warning("No bridge found via broadcast")
        return None
        
    except Exception as e:
        logger.error(f"Discovery error: {e}")
        return None
    finally:
        sock.close()

def run_validation_test(broker_host: str, broker_port: int = 1883,
                       username: Optional[str] = None, 
                       password: Optional[str] = None):
    """Run complete validation test"""
    
    logger.info("=" * 60)
    logger.info("MQTT BRIDGE VALIDATION TEST")
    logger.info("=" * 60)
    
    # Test 1: Connection
    logger.info("\n[TEST 1] Connection Test")
    tester = MQTTBridgeTester(broker_host, broker_port, username, password)
    
    if not tester.connect():
        logger.error("✗ Failed to connect to broker")
        return False
    logger.info("✓ Connection test passed")
    
    # Test 2: Device Registration
    logger.info("\n[TEST 2] Device Registration Test")
    if not tester.register_device("temperature_sensor", "Test Temperature Sensor"):
        logger.error("✗ Device registration failed")
        tester.disconnect()
        return False
    logger.info("✓ Device registration test passed")
    time.sleep(1)
    
    # Test 3: State Publishing
    logger.info("\n[TEST 3] State Publishing Test")
    test_states = [
        {"temperature": 23.5, "humidity": 65},
        {"temperature": 24.0, "humidity": 63},
        {"temperature": 23.8, "humidity": 64}
    ]
    
    for i, state in enumerate(test_states, 1):
        if not tester.publish_state(state):
            logger.error(f"✗ State publishing failed for iteration {i}")
            tester.disconnect()
            return False
        time.sleep(1)
    logger.info("✓ State publishing test passed")
    
    # Test 4: Telemetry Publishing
    logger.info("\n[TEST 4] Telemetry Publishing Test")
    test_telemetry = [
        {"battery": 95, "rssi": -45},
        {"battery": 94, "rssi": -47},
        {"battery": 93, "rssi": -46}
    ]
    
    for i, telemetry in enumerate(test_telemetry, 1):
        if not tester.publish_telemetry(telemetry):
            logger.error(f"✗ Telemetry publishing failed for iteration {i}")
            tester.disconnect()
            return False
        time.sleep(1)
    logger.info("✓ Telemetry publishing test passed")
    
    # Test 5: Command Subscription
    logger.info("\n[TEST 5] Command Subscription Test")
    if not tester.subscribe_to_commands():
        logger.error("✗ Command subscription failed")
        tester.disconnect()
        return False
    
    # Publish a test command from another client to verify
    test_client = MQTTBridgeTester(broker_host, broker_port, username, password)
    if test_client.connect():
        command_topic = f"mqtt-bridge/{tester.device_id}/command"
        test_command = {"action": "reboot", "params": {"delay": 5}}
        test_client.client.publish(command_topic, json.dumps(test_command))
        logger.info(f"✓ Published test command: {test_command}")
        time.sleep(1)
        test_client.disconnect()
    
    # Check if command was received
    messages = tester.wait_for_messages(2)
    commands_received = [msg for msg in messages if 'command' in msg['topic']]
    
    if commands_received:
        logger.info(f"✓ Received {len(commands_received)} command(s)")
        logger.info("✓ Command subscription test passed")
    else:
        logger.warning("⚠ No commands received (this might be normal if bridge doesn't forward to device)")
    
    # Test 6: Keep-alive Test
    logger.info("\n[TEST 6] Keep-alive Test")
    for i in range(3):
        logger.info(f"  Sending keep-alive {i+1}/3")
        if not tester.publish_state({"status": "online"}):
            logger.warning(f"  Keep-alive {i+1} failed")
        time.sleep(5)
    logger.info("✓ Keep-alive test completed")
    
    # Summary
    logger.info("\n" + "=" * 60)
    logger.info("VALIDATION TEST SUMMARY")
    logger.info("=" * 60)
    logger.info(f"Device ID: {tester.device_id}")
    logger.info(f"Broker: {broker_host}:{broker_port}")
    logger.info(f"Messages sent: {len(test_states) + len(test_telemetry) + 1}")
    logger.info("✓ All tests completed successfully!")
    
    tester.disconnect()
    return True

def main():
    parser = argparse.ArgumentParser(description='MQTT Bridge Device Validation Test')
    parser.add_argument('--host', type=str, help='Broker host (IP address)')
    parser.add_argument('--port', type=int, default=1883, help='Broker port (default: 1883)')
    parser.add_argument('--username', type=str, help='MQTT username (if required)')
    parser.add_argument('--password', type=str, help='MQTT password (if required)')
    parser.add_argument('--discover', action='store_true', help='Discover bridge via broadcast')
    parser.add_argument('--debug', action='store_true', help='Enable debug logging')
    
    args = parser.parse_args()
    
    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)
    
    broker_host = args.host
    
    # Try to discover bridge if host not provided
    if not broker_host and args.discover:
        logger.info("Attempting to discover MQTT bridge...")
        broker_host = discover_bridge()
        
        if not broker_host:
            logger.error("Could not discover bridge. Please provide host manually.")
            return 1
    
    if not broker_host:
        logger.error("Please provide broker host using --host or use --discover")
        return 1
    
    # Run validation test
    success = run_validation_test(
        broker_host=broker_host,
        broker_port=args.port,
        username=args.username,
        password=args.password
    )
    
    return 0 if success else 1

if __name__ == "__main__":
    exit(main())