#!/usr/bin/env python3
"""
Testes de validacao da API REST do ESP8266 On/Off.

Testa os endpoints:
  GET  /            -> pagina HTML
  GET  /api/state   -> JSON com estado atual
  POST /api/on      -> liga o rele
  POST /api/off     -> desliga o rele
  POST /api/toggle  -> inverte o estado

Uso:
  python3 test_rest_api.py --host <IP_do_ESP>
  python3 test_rest_api.py --host 192.168.1.50 --verbose
"""

import argparse
import json
import sys
import urllib.request
import urllib.error
from typing import Tuple


PASS = 0
FAIL = 1
_total = 0
_pass = 0

GREEN = "\033[92m"
RED = "\033[91m"
BOLD = "\033[1m"
RESET = "\033[0m"


def test(name: str, cond: bool, detail: str = "") -> None:
    global _total, _pass
    _total += 1
    if cond:
        _pass += 1
        print(f"  {GREEN}[PASS]{RESET} {name}")
    else:
        print(f"  {RED}[FAIL]{RESET} {name}" + (f"  {RED}{detail}{RESET}" if detail else ""))


def request(host: str, method: str, path: str) -> Tuple[int, str, str]:
    """Faz requisicao HTTP e retorna (status, body, content_type)."""
    url = f"http://{host}{path}"
    req = urllib.request.Request(url, method=method)
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            body = resp.read().decode()
            return resp.status, body, resp.headers.get("Content-Type", "")
    except urllib.error.HTTPError as e:
        body = e.read().decode() if e.fp else ""
        return e.code, body, e.headers.get("Content-Type", "") if e.headers else ""
    except Exception as e:
        return -1, str(e), ""


def run_tests(host: str, verbose: bool) -> None:
    print(f"\n{'='*60}")
    print(f"  ESP8266 On/Off - Testes da API REST")
    print(f"  Alvo: http://{host}")
    print(f"{'='*60}")

    # ---- 1. Pagina HTML ------------------------------------------------
    print("\n[1] GET /  -> pagina HTML")
    status, body, ct = request(host, "GET", "/")
    test("status 200", status == 200, f"status={status}")
    test("content-type text/html", "text/html" in ct, f"ct={ct}")
    test("contem <html>", "<html" in body or "<!DOCTYPE" in body)
    test("contem 'Ligar'", "Ligar" in body)
    test("contem 'Desligar'", "Desligar" in body)
    test("contem 'Inverter'", "Inverter" in body)
    test("contem /api/state", "/api/state" in body)
    if verbose and status == 200:
        print(f"  --- HTML snippet ---\n{body[:400]}\n  -------------------")

    # ---- 2. Estado inicial ---------------------------------------------
    print("\n[2] GET /api/state  -> estado inicial")
    status, body, ct = request(host, "GET", "/api/state")
    test("status 200", status == 200, f"status={status}")
    test("content-type application/json", "application/json" in ct, f"ct={ct}")
    data = {}
    if status == 200:
        try:
            data = json.loads(body)
            test("JSON valido", True)
            test("contem 'state'", "state" in data)
            test("'state' e booleano", isinstance(data.get("state"), bool))
            test("contem 'device_id'", "device_id" in data)
            test("contem 'ip'", "ip" in data)
            test("'ip' nao vazio", bool(data.get("ip")))
            if verbose:
                print(f"  --- JSON ---\n{json.dumps(data, indent=2)}\n  -----------")
        except json.JSONDecodeError:
            test("JSON valido", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status} body={body[:120]}{RESET}")
    initial_state = data.get("state", False)

    # ---- 3. POST /api/on -----------------------------------------------
    print("\n[3] POST /api/on  -> ligar")
    status, body, ct = request(host, "POST", "/api/on")
    test("status 200", status == 200, f"status={status}")
    test("content-type application/json", "application/json" in ct, f"ct={ct}")
    if status == 200:
        try:
            d = json.loads(body)
            test("JSON valido", True)
            test("status = ok", d.get("status") == "ok")
            test("state = true", d.get("state") is True)
        except json.JSONDecodeError:
            test("JSON valido", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status} body={body[:120]}{RESET}")

    # ---- 4. Confirma ON ------------------------------------------------
    print("\n[4] GET /api/state  -> confirma ON")
    status, body, _ = request(host, "GET", "/api/state")
    if status == 200:
        try:
            d = json.loads(body)
            test("state = true", d.get("state") is True)
        except json.JSONDecodeError:
            test("state = true", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status}{RESET}")

    # ---- 5. POST /api/off ----------------------------------------------
    print("\n[5] POST /api/off  -> desligar")
    status, body, ct = request(host, "POST", "/api/off")
    test("status 200", status == 200, f"status={status}")
    test("content-type application/json", "application/json" in ct, f"ct={ct}")
    if status == 200:
        try:
            d = json.loads(body)
            test("JSON valido", True)
            test("status = ok", d.get("status") == "ok")
            test("state = false", d.get("state") is False)
        except json.JSONDecodeError:
            test("JSON valido", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status} body={body[:120]}{RESET}")

    # ---- 6. Confirma OFF -----------------------------------------------
    print("\n[6] GET /api/state  -> confirma OFF")
    status, body, _ = request(host, "GET", "/api/state")
    if status == 200:
        try:
            d = json.loads(body)
            test("state = false", d.get("state") is False)
        except json.JSONDecodeError:
            test("state = false", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status}{RESET}")

    # ---- 7. POST /api/toggle (OFF->ON) ---------------------------------
    print("\n[7] POST /api/toggle  -> OFF -> ON")
    status, body, ct = request(host, "POST", "/api/toggle")
    test("status 200", status == 200, f"status={status}")
    test("content-type application/json", "application/json" in ct, f"ct={ct}")
    if status == 200:
        try:
            d = json.loads(body)
            test("JSON valido", True)
            test("status = ok", d.get("status") == "ok")
            test("state = true", d.get("state") is True)
        except json.JSONDecodeError:
            test("JSON valido", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status} body={body[:120]}{RESET}")

    # ---- 8. POST /api/toggle (ON->OFF) ---------------------------------
    print("\n[8] POST /api/toggle  -> ON -> OFF")
    status, body, ct = request(host, "POST", "/api/toggle")
    test("status 200", status == 200, f"status={status}")
    test("content-type application/json", "application/json" in ct, f"ct={ct}")
    if status == 200:
        try:
            d = json.loads(body)
            test("JSON valido", True)
            test("status = ok", d.get("status") == "ok")
            test("state = false", d.get("state") is False)
        except json.JSONDecodeError:
            test("JSON valido", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status} body={body[:120]}{RESET}")

    # ---- 9. Idempotencia: ON sobre ON ----------------------------------
    print("\n[9] Idempotencia - ON sobre ON")
    request(host, "POST", "/api/on")
    status, body, _ = request(host, "POST", "/api/on")
    test("status 200", status == 200, f"status={status}")
    if status == 200:
        try:
            d = json.loads(body)
            test("state = true", d.get("state") is True)
        except json.JSONDecodeError:
            test("state = true", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status}{RESET}")
    # confirma
    status, body, _ = request(host, "GET", "/api/state")
    if status == 200:
        try:
            test("estado ainda ON", json.loads(body).get("state") is True)
        except json.JSONDecodeError:
            test("estado ainda ON", False, f"body={body[:80]}")

    # ---- 10. Idempotencia: OFF sobre OFF -------------------------------
    print("\n[10] Idempotencia - OFF sobre OFF")
    request(host, "POST", "/api/off")
    status, body, _ = request(host, "POST", "/api/off")
    test("status 200", status == 200, f"status={status}")
    if status == 200:
        try:
            d = json.loads(body)
            test("state = false", d.get("state") is False)
        except json.JSONDecodeError:
            test("state = false", False, f"body={body[:80]}")
    else:
        print(f"  {RED}  resposta: status={status}{RESET}")
    # confirma
    status, body, _ = request(host, "GET", "/api/state")
    if status == 200:
        try:
            test("estado ainda OFF", json.loads(body).get("state") is False)
        except json.JSONDecodeError:
            test("estado ainda OFF", False, f"body={body[:80]}")

    # ---- 11. Restaura estado inicial -----------------------------------
    print("\n[11] Restaurar estado inicial")
    if initial_state:
        request(host, "POST", "/api/on")
    else:
        request(host, "POST", "/api/off")
    status, body, _ = request(host, "GET", "/api/state")
    if status == 200:
        try:
            test("estado restaurado", json.loads(body).get("state") is initial_state)
        except json.JSONDecodeError:
            test("estado restaurado", False, f"body={body[:80]}")

    # ---- Resumo ---------------------------------------------------------
    ok = _pass == _total
    print(f"\n{'='*60}")
    if ok:
        print(f"  {GREEN}{BOLD}[SUCESSO]{RESET} {_pass}/{_total} testes passaram")
    else:
        print(f"  {RED}{BOLD}[FALHA]{RESET} {_pass}/{_total} testes passaram")
    print(f"{'='*60}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Testa a API REST do ESP8266 On/Off")
    parser.add_argument("--host", required=True, help="Endereco IP do ESP8266")
    parser.add_argument("--verbose", "-v", action="store_true", help="Exibir detalhes")
    args = parser.parse_args()

    run_tests(args.host, args.verbose)
    return PASS if _pass == _total else FAIL


if __name__ == "__main__":
    sys.exit(main())
