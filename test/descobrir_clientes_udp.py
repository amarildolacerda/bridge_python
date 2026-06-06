#!/usr/bin/env python3
"""
Descobre bridge e clientes ESP via UDP na porta 5000.

Escuta passivamente broadcasts/anúncios e enviar discovery request
para forçar respostas.

Uso:
  python descobrir_clientes_udp.py                # escuta passivo
  python descobrir_clientes_udp.py --probe        # envia discovery + escuta
  python descobrir_clientes_udp.py --timeout 30   # escuta por 30s
"""
import socket
import json
import sys
import time
import argparse
import threading
from datetime import datetime

DISCOVERY_PORT = 5000
SERVICE_NAME = "esp-bridge"


def listen(stop_event, results):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", DISCOVERY_PORT))
    sock.settimeout(1)

    while not stop_event.is_set():
        try:
            data, addr = sock.recvfrom(1024)
            msg = data.decode("utf-8", errors="replace")
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            try:
                parsed = json.loads(msg)
            except json.JSONDecodeError:
                parsed = None

            results.append((ts, addr, msg, parsed))
        except socket.timeout:
            continue
        except Exception as e:
            print(f"  Erro: {e}", file=sys.stderr)

    sock.close()


def probe_bridge(sock):
    payload = json.dumps({"service": SERVICE_NAME, "discover": True, "id": "python_probe"})
    sock.sendto(payload.encode(), ("255.255.255.255", DISCOVERY_PORT))


def print_usage():
    print("─" * 70)
    print(f" UDP Discovery — escutando na porta {DISCOVERY_PORT}")
    print("─" * 70)


def print_summary(results, bridges, clients):
    print()
    print("═" * 70)
    print(" RESUMO")
    print("═" * 70)
    if bridges:
        print(f"\n Bridge(s) encontrado(s):")
        for ip, info in sorted(bridges.items()):
            print(f"   IP: {ip:15s}  porta: {info.get('http_port', '?')}")
    else:
        print(f"\n Nenhum bridge encontrado.")

    if clients:
        print(f"\n Cliente(s) encontrado(s) ({len(clients)}):")
        print(f"   {'ID':30s} {'IP':15s} {'Última vez':12s}")
        print(f"   {'─'*30} {'─'*15} {'─'*12}")
        for cid, info in sorted(clients.items(), key=lambda x: x[1]["last_seen"], reverse=True):
            print(f"   {cid:30s} {info['ip']:15s} {info['last_seen']:12s}")
    else:
        print(f"\n Nenhum cliente encontrado.")

    print(f"\n Total pacotes recebidos: {len(results)}")


def main():
    parser = argparse.ArgumentParser(description="Descobre bridge e clientes ESP via UDP")
    parser.add_argument("--probe", "-p", action="store_true", help="Envia discovery request ativo")
    parser.add_argument("--timeout", "-t", type=int, default=15, help="Tempo de escuta (s)")
    args = parser.parse_args()

    stop = threading.Event()
    results = []

    listener = threading.Thread(target=listen, args=(stop, results), daemon=True)
    listener.start()

    print_usage()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    if args.probe:
        print(f" Enviando discovery request na porta {DISCOVERY_PORT}...")
        probe_bridge(sock)
    else:
        print(f" Modo passivo — aguardando broadcasts...")
        print(f" Use --probe -p para enviar discovery request ativo.")
        print(f"     --timeout -t <n>")

    print(f" Escutando por {args.timeout}s (Ctrl+C para encerrar)")
    print()

    deadline = time.time() + args.timeout
    bridges = {}
    clients = {}

    try:
        while time.time() < deadline:
            while results:
                ts, addr, raw, parsed = results.pop(0)

                if parsed is None:
                    print(f" [{ts}] {addr[0]:15s} [dado inválido] {raw[:80]}")
                    continue

                svc = parsed.get("service", "")
                ip = parsed.get("ip_sta", addr[0])
                cid = parsed.get("id", "")

                if "esp-bridge" in svc:
                    bridges[ip] = parsed
                    uptime = parsed.get("uptime_s", "?")
                    print(f" [{ts}] BRIDGE {ip:15s}  uptime={uptime}s  {raw[:120]}")

                elif cid:
                    clients[cid] = {"ip": addr[0], "last_seen": ts}
                    typ = parsed.get("type", parsed.get("device_type", "?"))
                    print(f" [{ts}] CLIENTE {cid:30s}  IP={addr[0]:15s}  type={typ}  {raw[:120]}")

                else:
                    print(f" [{ts}] {addr[0]:15s} {raw[:120]}")

            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\n\nEncerrando...")

    sock.close()
    stop.set()
    listener.join(timeout=3)

    print_summary(results, bridges, clients)

    return 0 if bridges else 1


if __name__ == "__main__":
    sys.exit(main())
