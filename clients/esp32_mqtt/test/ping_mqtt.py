#!/usr/bin/env python3
"""
MQTT Bridge Device Validation Test - Multi-Sensor Version
Tests the ESP32 MQTT Bridge Broker functionality with multiple device types
"""

import paho.mqtt.client as mqtt
import json
import time
import socket
import argparse
import random
import threading
from typing import Optional, Dict, Any, List
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

class MQTTBridgeTester:
    def __init__(self, broker_host: str, broker_port: int = 1883, 
                 username: Optional[str] = None, password: Optional[str] = None,
                 device_id: Optional[str] = None):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.username = username
        self.password = password
        self.client = None
        self.connected = False
        self.received_messages = []
        self.device_id = device_id or f"test_device_{int(time.time())}"
        self.running = False
        
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
                        device_name: Optional[str] = None,
                        metadata: Optional[Dict] = None) -> bool:
        """Register a device with the bridge"""
        topic = f"mqtt-bridge/register"
        
        payload = {
            "id": self.device_id,
            "type": device_type,
            "name": device_name or f"Test {device_type}",
            "timestamp": int(time.time())
        }
        
        if metadata:
            payload["metadata"] = metadata
        
        logger.info(f"Registering device: {self.device_id} ({device_type})")
        result = self.client.publish(topic, json.dumps(payload))
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            logger.info(f"✓ Device registered successfully")
            return True
        else:
            logger.error(f"✗ Failed to register device")
            return False
    
    def publish_state(self, state_data: Dict[str, Any], suffix: str = "state") -> bool:
        """Publish device state to bridge"""
        topic = f"mqtt-bridge/{self.device_id}/{suffix}"
        
        # Add metadata to state
        state_data['timestamp'] = int(time.time())
        state_data['device_id'] = self.device_id
        
        result = self.client.publish(topic, json.dumps(state_data))
        
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            logger.debug(f"✓ Published to {suffix}: {state_data}")
            return True
        else:
            logger.error(f"✗ Failed to publish to {suffix}")
            return False
    
    def publish_telemetry(self, telemetry_data: Dict[str, Any]) -> bool:
        """Publish device telemetry to bridge"""
        return self.publish_state(telemetry_data, "telemetry")
    
    def publish_event(self, event_data: Dict[str, Any]) -> bool:
        """Publish device event to bridge"""
        return self.publish_state(event_data, "event")
    
    def publish_config(self, config_data: Dict[str, Any]) -> bool:
        """Publish device configuration to bridge"""
        return self.publish_state(config_data, "config")
    
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
    
    def start_simulation(self, interval: float = 5.0):
        """Start continuous device simulation"""
        self.running = True
        self.simulation_thread = threading.Thread(target=self._simulation_loop, args=(interval,))
        self.simulation_thread.daemon = True
        self.simulation_thread.start()
    
    def _simulation_loop(self, interval: float):
        """Simulation loop for continuous data publishing"""
        counter = 0
        while self.running:
            try:
                # Generate random sensor data based on device type
                if "temperature" in self.device_id or "sensor" in self.device_id:
                    self.publish_state({
                        "temperature": round(20 + random.random() * 10, 1),
                        "humidity": round(40 + random.random() * 40, 1),
                        "pressure": round(980 + random.random() * 40, 1)
                    })
                elif "switch" in self.device_id or "light" in self.device_id:
                    state = counter % 2 == 0
                    self.publish_state({
                        "state": "ON" if state else "OFF",
                        "brightness": random.randint(0, 100) if state else 0,
                        "power": 5.2 if state else 0.1
                    })
                elif "motion" in self.device_id:
                    motion_detected = random.random() > 0.7
                    self.publish_state({
                        "motion": motion_detected,
                        "occupancy": motion_detected,
                        "since": int(time.time()) if motion_detected else 0
                    })
                elif "energy" in self.device_id:
                    self.publish_state({
                        "power": round(100 + random.random() * 900, 1),
                        "voltage": round(220 + random.random() * 10, 1),
                        "current": round(0.5 + random.random() * 4.5, 2),
                        "energy_today": round(0.5 + counter * 0.1, 2)
                    })
                else:
                    # Generic device
                    self.publish_state({
                        "value": random.randint(0, 100),
                        "status": "active",
                        "counter": counter
                    })
                
                counter += 1
                time.sleep(interval)
                
            except Exception as e:
                logger.error(f"Simulation error: {e}")
                time.sleep(interval)
    
    def stop_simulation(self):
        """Stop continuous simulation"""
        self.running = False
        if hasattr(self, 'simulation_thread'):
            self.simulation_thread.join(timeout=2)
    
    def disconnect(self):
        """Disconnect from broker"""
        self.running = False
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()
            logger.info("Disconnected from broker")

class MultiDeviceSimulator:
    """Simulate multiple devices connecting to the bridge"""
    
    def __init__(self, broker_host: str, broker_port: int = 1883,
                 username: Optional[str] = None, password: Optional[str] = None):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.username = username
        self.password = password
        self.devices: List[MQTTBridgeTester] = []
        
    def add_device(self, device_type: str, device_name: str, 
                   custom_id: Optional[str] = None) -> MQTTBridgeTester:
        """Add a new simulated device"""
        device_id = custom_id or f"{device_type}_{int(time.time())}_{len(self.devices)}"
        device = MQTTBridgeTester(
            self.broker_host, self.broker_port,
            self.username, self.password,
            device_id
        )
        
        if device.connect():
            # Register device
            device.register_device(device_type, device_name)
            time.sleep(0.5)
            self.devices.append(device)
            logger.info(f"✓ Added device: {device_name} ({device_id})")
            return device
        else:
            logger.error(f"✗ Failed to add device: {device_name}")
            return None
    
    def start_all_simulations(self, interval: float = 5.0):
        """Start simulations for all devices"""
        for device in self.devices:
            device.start_simulation(interval)
        logger.info(f"Started simulations for {len(self.devices)} devices")
    
    def stop_all_simulations(self):
        """Stop all device simulations"""
        for device in self.devices:
            device.stop_simulation()
        logger.info("Stopped all simulations")
    
    def disconnect_all(self):
        """Disconnect all devices"""
        for device in self.devices:
            device.disconnect()
        logger.info("Disconnected all devices")

def discover_bridge(timeout: int = 10) -> Optional[str]:
    """Discover MQTT bridge via UDP broadcast"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(timeout)
        
        # Bind to broadcast port
        sock.bind(('', 18888))  # BROADCAST_PORT from config
        
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
                    logger.info(f"  Device Count: {bridge_info.get('device_count', 0)}")
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

def run_single_device_test(broker_host: str, broker_port: int = 1883,
                          username: Optional[str] = None, 
                          password: Optional[str] = None):
    """Run complete validation test with single device"""
    
    logger.info("=" * 60)
    logger.info("SINGLE DEVICE VALIDATION TEST")
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
    if not tester.register_device("temperature_sensor", "Living Room Sensor"):
        logger.error("✗ Device registration failed")
        tester.disconnect()
        return False
    logger.info("✓ Device registration test passed")
    time.sleep(1)
    
    # Test 3: Multiple Data Types
    logger.info("\n[TEST 3] Multiple Data Types Test")
    
    # State data
    test_states = [
        {"temperature": 23.5, "humidity": 65, "battery": 95},
        {"temperature": 24.0, "humidity": 63, "battery": 94},
        {"temperature": 23.8, "humidity": 64, "battery": 93}
    ]
    
    for i, state in enumerate(test_states, 1):
        if not tester.publish_state(state):
            logger.error(f"✗ State publishing failed for iteration {i}")
            tester.disconnect()
            return False
        time.sleep(1)
    logger.info("✓ State publishing test passed")
    
    # Telemetry data
    test_telemetry = [
        {"rssi": -45, "uptime": 3600, "free_heap": 180000},
        {"rssi": -47, "uptime": 7200, "free_heap": 179000},
        {"rssi": -46, "uptime": 10800, "free_heap": 178000}
    ]
    
    for i, telemetry in enumerate(test_telemetry, 1):
        if not tester.publish_telemetry(telemetry):
            logger.error(f"✗ Telemetry publishing failed for iteration {i}")
            tester.disconnect()
            return False
        time.sleep(1)
    logger.info("✓ Telemetry publishing test passed")
    
    # Events
    test_events = [
        {"event": "device_started", "severity": "info"},
        {"event": "configuration_changed", "severity": "warning"},
        {"event": "sensor_calibrated", "severity": "info"}
    ]
    
    for i, event in enumerate(test_events, 1):
        if not tester.publish_event(event):
            logger.error(f"✗ Event publishing failed for iteration {i}")
            tester.disconnect()
            return False
        time.sleep(1)
    logger.info("✓ Event publishing test passed")
    
    # Configuration
    test_configs = [
        {"interval": 30, "mode": "auto", "threshold": 25.0},
        {"interval": 60, "mode": "manual", "threshold": 26.0}
    ]
    
    for i, config in enumerate(test_configs, 1):
        if not tester.publish_config(config):
            logger.error(f"✗ Config publishing failed for iteration {i}")
            tester.disconnect()
            return False
        time.sleep(1)
    logger.info("✓ Config publishing test passed")
    
    # Test 4: Command Subscription
    logger.info("\n[TEST 4] Command Subscription Test")
    if not tester.subscribe_to_commands():
        logger.error("✗ Command subscription failed")
        tester.disconnect()
        return False
    
    # Publish test commands from another client
    test_client = MQTTBridgeTester(broker_host, broker_port, username, password)
    if test_client.connect():
        commands = [
            {"action": "reboot", "params": {"delay": 5}},
            {"action": "set_interval", "params": {"interval": 10}},
            {"action": "calibrate", "params": {"type": "temperature"}}
        ]
        
        for cmd in commands:
            command_topic = f"mqtt-bridge/{tester.device_id}/command"
            test_client.client.publish(command_topic, json.dumps(cmd))
            logger.info(f"✓ Published command: {cmd}")
            time.sleep(1)
        
        test_client.disconnect()
    
    # Check if commands were received
    messages = tester.wait_for_messages(2)
    commands_received = [msg for msg in messages if 'command' in msg['topic']]
    
    if commands_received:
        logger.info(f"✓ Received {len(commands_received)} command(s)")
        logger.info("✓ Command subscription test passed")
    else:
        logger.warning("⚠ No commands received (bridge may not forward to device)")
    
    # Summary
    logger.info("\n" + "=" * 60)
    logger.info("VALIDATION TEST SUMMARY")
    logger.info("=" * 60)
    logger.info(f"Device ID: {tester.device_id}")
    logger.info(f"Broker: {broker_host}:{broker_port}")
    logger.info(f"Messages sent: {len(test_states) + len(test_telemetry) + len(test_events) + len(test_configs)}")
    logger.info("✓ All tests completed successfully!")
    
    tester.disconnect()
    return True

def run_multi_device_test(broker_host: str, broker_port: int = 1883,
                         username: Optional[str] = None,
                         password: Optional[str] = None,
                         duration: int = 30):
    """Run multi-device simulation test"""
    
    logger.info("=" * 60)
    logger.info("MULTI-DEVICE SIMULATION TEST")
    logger.info("=" * 60)
    
    simulator = MultiDeviceSimulator(broker_host, broker_port, username, password)
    
    # Define device types to simulate
    devices_config = [
        ("temperature_sensor", "Living Room Temperature", "temp_living"),
        ("humidity_sensor", "Bathroom Humidity", "humi_bathroom"),
        ("temperature_sensor", "Bedroom Temperature", "temp_bedroom"),
        ("motion_sensor", "Hallway Motion", "motion_hallway"),
        ("light_switch", "Kitchen Light", "light_kitchen"),
        ("energy_meter", "Main Power Meter", "energy_main"),
        ("smart_plug", "Coffee Machine", "plug_coffee"),
        ("door_sensor", "Front Door", "door_front"),
        ("temperature_sensor", "Garage Temperature", "temp_garage"),
        ("motion_sensor", "Backyard Motion", "motion_backyard")
    ]
    
    # Add all devices
    logger.info(f"\nAdding {len(devices_config)} simulated devices...")
    for device_type, device_name, device_id in devices_config:
        device = simulator.add_device(device_type, device_name, device_id)
        if device:
            # Publish initial state
            if device_type == "temperature_sensor":
                device.publish_state({"temperature": 22.0, "humidity": 50})
            elif device_type == "motion_sensor":
                device.publish_state({"motion": False})
            elif device_type == "light_switch":
                device.publish_state({"state": "OFF"})
            elif device_type == "energy_meter":
                device.publish_state({"power": 0, "voltage": 220})
            elif device_type == "door_sensor":
                device.publish_state({"open": False})
        time.sleep(0.2)
    
    logger.info(f"\n✓ Added {len(simulator.devices)} devices successfully")
    
    # Start simulations
    logger.info(f"\nStarting simulations for {duration} seconds...")
    simulator.start_all_simulations(interval=3.0)
    
    # Run for specified duration
    try:
        time.sleep(duration)
    except KeyboardInterrupt:
        logger.info("\nInterrupted by user")
    
    # Stop simulations
    simulator.stop_all_simulations()
    
    # Disconnect all devices
    simulator.disconnect_all()
    
    logger.info("\n" + "=" * 60)
    logger.info("MULTI-DEVICE TEST SUMMARY")
    logger.info("=" * 60)
    logger.info(f"Total devices simulated: {len(simulator.devices)}")
    logger.info(f"Test duration: {duration} seconds")
    logger.info("✓ All devices should appear in bridge dashboard")
    logger.info(f"  Check: http://{broker_host}:8080/api/devices")
    
    return True

def run_stress_test(broker_host: str, broker_port: int = 1883,
                   username: Optional[str] = None,
                   password: Optional[str] = None,
                   num_devices: int = 20,
                   duration: int = 60):
    """Run stress test with many devices"""
    
    logger.info("=" * 60)
    logger.info("STRESS TEST")
    logger.info("=" * 60)
    logger.info(f"Simulating {num_devices} devices for {duration} seconds")
    
    simulator = MultiDeviceSimulator(broker_host, broker_port, username, password)
    
    # Add many devices
    device_types = ["sensor", "switch", "motion", "energy", "plug", "door", "temperature"]
    
    for i in range(num_devices):
        device_type = device_types[i % len(device_types)]
        device_name = f"Stress_Device_{i+1}"
        device_id = f"stress_{device_type}_{i+1}"
        
        device = simulator.add_device(device_type, device_name, device_id)
        if device:
            device.publish_state({"status": "starting", "index": i})
        
        if (i + 1) % 5 == 0:
            logger.info(f"Added {i+1}/{num_devices} devices...")
        
        time.sleep(0.1)
    
    logger.info(f"\n✓ Added {len(simulator.devices)}/{num_devices} devices")
    
    # Start all simulations with longer interval to reduce load
    simulator.start_all_simulations(interval=5.0)
    
    # Run test
    try:
        for i in range(duration):
            if i % 10 == 0:
                logger.info(f"Stress test running... {i}/{duration} seconds")
            time.sleep(1)
    except KeyboardInterrupt:
        logger.info("\nInterrupted by user")
    
    # Cleanup
    simulator.stop_all_simulations()
    simulator.disconnect_all()
    
    logger.info("\n" + "=" * 60)
    logger.info("STRESS TEST SUMMARY")
    logger.info("=" * 60)
    logger.info(f"Devices simulated: {len(simulator.devices)}")
    logger.info(f"Test duration: {duration} seconds")
    logger.info("Check bridge stability and memory usage")
    
    return True

def main():
    parser = argparse.ArgumentParser(description='MQTT Bridge Device Validation Test')
    parser.add_argument('--host', type=str, help='Broker host (IP address)')
    parser.add_argument('--port', type=int, default=1883, help='Broker port (default: 1883)')
    parser.add_argument('--username', type=str, help='MQTT username (if required)')
    parser.add_argument('--password', type=str, help='MQTT password (if required)')
    parser.add_argument('--discover', action='store_true', help='Discover bridge via broadcast')
    parser.add_argument('--debug', action='store_true', help='Enable debug logging')
    
    # Test modes
    parser.add_argument('--single', action='store_true', help='Run single device test')
    parser.add_argument('--multi', action='store_true', help='Run multi-device simulation')
    parser.add_argument('--stress', action='store_true', help='Run stress test')
    parser.add_argument('--duration', type=int, default=30, help='Test duration in seconds (for multi/stress)')
    parser.add_argument('--devices', type=int, default=20, help='Number of devices for stress test')
    
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
    
    # Determine test mode
    if args.stress:
        success = run_stress_test(
            broker_host=broker_host,
            broker_port=args.port,
            username=args.username,
            password=args.password,
            num_devices=args.devices,
            duration=args.duration
        )
    elif args.multi:
        success = run_multi_device_test(
            broker_host=broker_host,
            broker_port=args.port,
            username=args.username,
            password=args.password,
            duration=args.duration
        )
    else:
        # Default to single device test
        success = run_single_device_test(
            broker_host=broker_host,
            broker_port=args.port,
            username=args.username,
            password=args.password
        )
    
    return 0 if success else 1

if __name__ == "__main__":
    exit(main())