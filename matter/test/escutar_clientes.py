#!/usr/bin/env python3
"""
Listener de Clientes ESP8266 via Bridge API + WebSocket
Uso: python escutar_clientes.py [--host 192.168.1.202] [--poll 2]
"""
import json, sys, time, argparse
from datetime import datetime
try:
    from websocket import create_connection
except ImportError:
    import subprocess
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'websocket-client', '-q'])
    from websocket import create_connection

try:
    import urllib.request
except ImportError:
    import urllib2 as urllib_request

def fetch_json(url, timeout=3):
    resp = urllib.request.urlopen(url, timeout=timeout)
    return json.loads(resp.read())

def print_device(d, now):
    ip = d.get("ip", "?")
    name = d.get("name", d.get("id", "?"))
    typ = d.get("type", "?")
    ep = d.get("endpoint_id", "?")
    online = "ON" if d.get("online") else "OFF"
    state = d.get("state", {})
    state_str = ", ".join(f"{k}={v}" for k, v in state.items()) if state else "-"
    print(f" {name:20s}  type={typ:12s}  ep={ep:>3}  {online}  ip={ip:15s}  {state_str}")

def main():
    parser = argparse.ArgumentParser(description="Escuta clientes ESP8266 via bridge")
    parser.add_argument("--host", default="192.168.1.202", help="IP do bridge")
    parser.add_argument("--poll", type=int, default=2, help="intervalo de polling (s)")
    parser.add_argument("--ws", action="store_true", help="usar WebSocket em vez de polling")
    args = parser.parse_args()

    base = f"http://{args.host}"
    ws_url = f"ws://{args.host}/ws"

    print(f"Escutando clientes em {base}")
    print(f"Polling a cada {args.poll}s" if not args.ws else f"WebSocket: {ws_url}")
    print("-" * 90)
    print(f"{'HORA':8s}  {'DISPOSITIVO':22s}  {'TIPO':12s}  {'EP':>3}  {'STATUS':4s}  {'IP':15s}  {'ESTADO'}")
    print("-" * 90)

    prev = {}

    if args.ws:
        ws = create_connection(ws_url, timeout=5)
        print(f"WebSocket conectado em {args.host}/ws")
        try:
            while True:
                data = ws.recv()
                parsed = json.loads(data)
                ts = datetime.now().strftime("%H:%M:%S")
                uptime = parsed.get("uptime_s", 0)
                heap = parsed.get("free_heap", 0)
                print(f"  [{ts}] bridge | uptime={uptime}s heap={heap}B")
                sys.stdout.flush()
        except KeyboardInterrupt:
            ws.close()
            print("\nEncerrado")
        return

    try:
        while True:
            now = datetime.now().strftime("%H:%M:%S")
            try:
                data = fetch_json(f"{base}/api/devices")
                devices = data.get("devices", [])
                if not devices:
                    print(f"  [{now}] (nenhum cliente conectado)")
                else:
                    for d in devices:
                        did = d.get("id", "")
                        prev_state = prev.get(did, {}).get("state")
                        cur_state = d.get("state")
                        changed = prev_state and cur_state and prev_state != cur_state
                        marker = "*" if changed else " "
                        print(f" [{now}]{marker}", end="")
                        print_device(d, now)
                        prev[did] = d
            except Exception as e:
                print(f"  [{now}] ERRO: {e}")
            sys.stdout.flush()
            time.sleep(args.poll)
    except KeyboardInterrupt:
        print("\nEncerrado")

if __name__ == "__main__":
    main()
