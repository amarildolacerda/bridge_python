# Git Pull Button Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Force Update" MQTT button entity in Home Assistant that executes git pull on the bridge-python add-on source and restarts automatically.

**Architecture:** MQTT button entity → bridge subscribes to command topic → `git pull --ff-only` in mounted addon source → exit process → Supervisor watchdog restarts container with updated code.

**Tech Stack:** FastAPI, aiomqtt, HA Supervisor add-on, git

## Global Constraints

- `.git/` must be included in add-on installation (not excluded by rsync)
- git pull uses `--ff-only` to prevent merge conflicts
- The add-on must work both with and without the `/addons` mount (fallback to baked-in code)
- FW_VERSION must match current tag across all bridges and clients
- All changes only on `dev` branch

---

### Task 1: Add-On Infrastructure (config.yaml + Dockerfile)

**Files:**
- Modify: `bridge_python/config.yaml`
- Modify: `bridge_python/Dockerfile`
- Test: `bridge_python/tests/test_docker.py` (check git installed)

**Interfaces:**
- Consumes: current config.yaml and Dockerfile
- Produces: add-on with git + watchdog + addons mount

- [ ] **Modify config.yaml: add `map: addons:rw` and `watchdog: true`**

```yaml
# After line 16 (config:rw), add:
  - addons:rw
# After line 13 (init: false), add:
watchdog: true
```

Edit `bridge_python/config.yaml`:
- Add `- addons:rw` to the `map` list
- Add `watchdog: true` after `init: false`

- [ ] **Modify Dockerfile: install git, change CMD to use run.sh**

```dockerfile
# After FROM line, add git install:
RUN apt-get update && apt-get install -y --no-install-recommends git && rm -rf /var/lib/apt/lists/*

# After the existing CMD, replace with:
CMD ["sh", "-c", "exec /app/run.sh"]
```

Edit `bridge_python/Dockerfile`:
- Add `RUN apt-get update && apt-get install -y --no-install-recommends git && rm -rf /var/lib/apt/lists/*` after the `FROM` line
- Replace the `CMD` line with `CMD ["sh", "-c", "exec /app/run.sh"]`

- [ ] **Verify git is available in the image**

Run: `docker build -t bridge-python-test -f bridge_python/Dockerfile bridge_python/`
Run: `docker run --rm bridge-python-test git --version`
Expected: git version output

---

### Task 2: install_addon.sh — include .git/ in rsync

**Files:**
- Modify: `bridge_python/install_addon.sh`

- [ ] **Remove `--exclude='.git/'` from rsync**

Edit `bridge_python/install_addon.sh` line 41: remove `--exclude='.git/' \`

- [ ] **Verify the change**

Run: `grep 'exclude.*.git' bridge_python/install_addon.sh`
Expected: no output (no .git exclusion)

---

### Task 3: run.sh — detect and export BRIDGE_SRC_DIR

**Files:**
- Modify: `bridge_python/run.sh`

- [ ] **Add BRIDGE_SRC_DIR auto-detection before the exec line**

Edit `bridge_python/run.sh`: after `mkdir -p /data/bridge_python` and before `exec`:

```bash
# Auto-detect source directory (mounted addon path takes precedence)
if [ -f "/addons/esp32_bridge_python/app/main.py" ]; then
    export BRIDGE_SRC_DIR="/addons/esp32_bridge_python"
elif [ -f "/app/app/main.py" ]; then
    export BRIDGE_SRC_DIR="/app"
else
    export BRIDGE_SRC_DIR=""
fi

exec python3 -m app.main
```

---

### Task 4: config.py — add addon_slug setting

**Files:**
- Modify: `bridge_python/app/config.py`

- [ ] **Add addon_slug field**

Edit `bridge_python/app/config.py`: add after `data_dir`:

```python
addon_slug: str = "esp32_bridge_python"
```

---

### Task 5: MQTT Button Entity — publish discovery + subscribe commands

**Files:**
- Modify: `bridge_python/app/mqtt_discovery.py`
- Modify: `bridge_python/app/main.py`
- Test: `bridge_python/tests/test_mqtt_discovery.py`

**Interfaces:**
- Consumes: `MQTTDiscovery` class
- Produces: `MQTTDiscovery.publish_force_update_config()`, `MQTTDiscovery.publish_force_update_result()`, `MQTTDiscovery.remove_force_update_config()`

- [ ] **Write test for force_update button discovery config**

Add to `bridge_python/tests/test_mqtt_discovery.py`:

```python
@pytest.mark.asyncio
async def test_publish_force_update_config(self, connected_mqtt):
    await connected_mqtt.publish_force_update_config()
    connected_mqtt._client.publish.assert_called_once()
    args, kwargs = connected_mqtt._client.publish.call_args
    topic = args[0]
    assert topic == "homeassistant/button/esp32_bridge_host/force_update/config"
    assert kwargs["retain"] is True
    payload = json.loads(kwargs["payload"])
    assert payload["platform"] == "button"
    assert payload["command_topic"] == "esp32-bridge/force_update/set"
    assert payload["payload_press"] == "PRESS"
    assert payload["unique_id"] == "esp32_bridge_force_update"
    assert payload["device"]["identifiers"] == ["esp32_bridge_host"]
```

- [ ] **Run test to verify it fails**

Run: `cd bridge_python && python -m pytest tests/test_mqtt_discovery.py::TestMQTTDiscovery::test_publish_force_update_config -v`
Expected: AttributeError (method not defined)

- [ ] **Add `publish_force_update_config` to MQTTDiscovery**

Add to `bridge_python/app/mqtt_discovery.py` after `remove_device_config`:

```python
FORCE_UPDATE_BUTTON_CONFIG = {
    "platform": "button",
    "name": "Force Update",
    "unique_id": "esp32_bridge_force_update",
    "device_class": "update",
    "icon": "mdi:cloud-download",
    "command_topic": "esp32-bridge/force_update/set",
    "payload_press": "PRESS",
    "device": {
        "identifiers": ["esp32_bridge_host"],
        "name": "ESP32 Bridge Host",
        "sw_version": "bridge_python_v0.0.10",
        "manufacturer": "ESP-HA Bridge",
        "model": "bridge",
    },
}

async def publish_force_update_config(self):
    if not self._connected:
        return
    topic = f"{DISCOVERY_PREFIX}/button/esp32_bridge_host/force_update/config"
    await self._publish(topic, json.dumps(self.FORCE_UPDATE_BUTTON_CONFIG), retain=True)

async def publish_force_update_result(self, success: bool, message: str):
    payload = json.dumps({"success": success, "message": message})
    await self.publish("esp32-bridge/force_update/result", payload)

async def remove_force_update_config(self):
    if not self._connected:
        return
    topic = f"{DISCOVERY_PREFIX}/button/esp32_bridge_host/force_update/config"
    await self._publish(topic, "", retain=True)
```

- [ ] **Run test to verify it passes**

Run: `cd bridge_python && python -m pytest tests/test_mqtt_discovery.py::TestMQTTDiscovery::test_publish_force_update_config -v`
Expected: PASS

- [ ] **Call `publish_force_update_config` in startup**

Edit `bridge_python/app/main.py` in the `startup()` function, after `await mqtt.publish_device_config(dev)` loop:

```python
await mqtt.publish_force_update_config()
```

Also add to `mqtt_state_sync` or a new task: subscribe to command topic.

- [ ] **Add MQTT subscription for force_update command topic**

Edit `bridge_python/app/main.py`: add new async function using a dedicated MQTT connection (separate from the publishing client) and wire it in startup:

```python
async def force_update_listener():
    import aiomqtt
    while True:
        try:
            async with aiomqtt.Client(
                hostname=settings.mqtt_host,
                port=settings.mqtt_port,
                username=settings.mqtt_user or None,
                password=settings.mqtt_pass or None,
            ) as client:
                async with client.messages() as messages:
                    await client.subscribe("esp32-bridge/force_update/set")
                    async for message in messages:
                        payload = message.payload.decode()
                        if payload == "PRESS":
                            LOG.info("Force update triggered via MQTT")
                            asyncio.create_task(handle_force_update())
        except Exception:
            LOG.exception("Force update listener error, retrying in 10s")
            await asyncio.sleep(10)

async def startup():
    # ... existing code ...
    asyncio.create_task(force_update_listener())
```

- [ ] **Commit Task 5**

```bash
git add bridge_python/app/mqtt_discovery.py bridge_python/app/main.py bridge_python/tests/test_mqtt_discovery.py
git commit -m "feat: add MQTT button entity for force update"
```

---

### Task 6: Git Pull Logic (http_api.py endpoint + handler)

**Files:**
- Modify: `bridge_python/app/http_api.py`
- Create: `bridge_python/app/git_pull.py`
- Test: `bridge_python/tests/test_git_pull.py`

**Interfaces:**
- Consumes: `settings.addon_slug`, env `BRIDGE_SRC_DIR`
- Produces: `git_pull()` function returning `dict`, `POST /api/gateway/git-pull`

- [ ] **Write tests for git_pull logic**

Create `bridge_python/tests/test_git_pull.py`:

```python
from __future__ import annotations
import json
import os
import subprocess
import tempfile
import pytest
from app.git_pull import git_pull, detect_repo_path


def test_detect_repo_path_with_env(monkeypatch):
    monkeypatch.setenv("BRIDGE_SRC_DIR", "/some/path")
    assert detect_repo_path() == "/some/path"


def test_detect_repo_path_not_found(monkeypatch):
    monkeypatch.delenv("BRIDGE_SRC_DIR", raising=False)
    result = detect_repo_path()
    assert result == ""


def test_git_pull_not_git_repo(tmp_path):
    result = git_pull(str(tmp_path))
    assert result["success"] is False
    assert "not a git repository" in result["message"].lower()


def test_git_pull_success():
    with tempfile.TemporaryDirectory() as tmpdir:
        subprocess.run(["git", "init"], cwd=tmpdir, capture_output=True)
        subprocess.run(["git", "config", "user.email", "test@test.com"], cwd=tmpdir, capture_output=True)
        subprocess.run(["git", "config", "user.name", "Test"], cwd=tmpdir, capture_output=True)
        subprocess.run(["git", "commit", "--allow-empty", "-m", "initial"], cwd=tmpdir, capture_output=True)
        result = git_pull(tmpdir)
        assert result["success"] is True
        assert result["updated"] is False  # no remote, so no changes
        assert "already up to date" in result["message"].lower() or "no remote" in result["message"].lower()


def test_git_pull_with_ff_only_flag(tmp_path):
    (tmp_path / "test.txt").write_text("hello")
    subprocess.run(["git", "init"], cwd=tmp_path, capture_output=True)
    subprocess.run(["git", "config", "user.email", "test@test.com"], cwd=tmp_path, capture_output=True)
    subprocess.run(["git", "config", "user.name", "Test"], cwd=tmp_path, capture_output=True)
    subprocess.run(["git", "add", "."], cwd=tmp_path, capture_output=True)
    subprocess.run(["git", "commit", "-m", "initial"], cwd=tmp_path, capture_output=True)
    result = git_pull(tmp_path)
    assert result["success"] is True
```

- [ ] **Run test to verify it fails**

Run: `cd bridge_python && python -m pytest tests/test_git_pull.py -v`
Expected: ImportError (no module git_pull)

- [ ] **Create `bridge_python/app/git_pull.py`**

```python
from __future__ import annotations
import logging
import os
import subprocess

LOG = logging.getLogger(__name__)


def detect_repo_path() -> str:
    src_dir = os.environ.get("BRIDGE_SRC_DIR", "")
    if src_dir and os.path.isdir(os.path.join(src_dir, ".git")):
        return src_dir
    candidates = [
        "/addons/esp32_bridge_python",
        "/app",
    ]
    for path in candidates:
        if os.path.isdir(os.path.join(path, ".git")):
            return path
    return os.environ.get("BRIDGE_SRC_DIR", "")


def git_pull(repo_path: str | None = None) -> dict:
    if repo_path is None:
        repo_path = detect_repo_path()
    if not repo_path:
        return {"success": False, "updated": False, "message": "Repository path not found", "output": ""}
    git_dir = os.path.join(repo_path, ".git")
    if not os.path.isdir(git_dir):
        return {"success": False, "updated": False, "message": "Not a git repository", "output": ""}
    try:
        LOG.info("Running git fetch in %s", repo_path)
        fetch = subprocess.run(
            ["git", "-C", repo_path, "fetch", "--all"],
            capture_output=True, text=True, timeout=30,
        )
        LOG.info("Fetch: %s", fetch.stdout.strip())
        if fetch.returncode != 0:
            return {"success": False, "updated": False, "message": "Fetch failed", "output": fetch.stderr.strip()}

        log_result = subprocess.run(
            ["git", "-C", repo_path, "log", "HEAD..origin/main", "--oneline"],
            capture_output=True, text=True, timeout=10,
        )
        commits = [c for c in log_result.stdout.strip().split("\n") if c]

        if not commits:
            return {"success": True, "updated": False, "message": "Already up to date", "output": "", "commits": []}

        LOG.info("Commits to pull: %d", len(commits))
        pull = subprocess.run(
            ["git", "-C", repo_path, "pull", "--ff-only"],
            capture_output=True, text=True, timeout=30,
        )
        LOG.info("Pull: %s", pull.stdout.strip())
        if pull.returncode != 0:
            return {"success": False, "updated": False, "message": "Pull failed", "output": pull.stderr.strip(), "commits": commits}

        return {
            "success": True,
            "updated": True,
            "message": f"Updated with {len(commits)} new commit(s)",
            "commits": commits,
            "output": pull.stdout.strip(),
        }
    except subprocess.TimeoutExpired:
        return {"success": False, "updated": False, "message": "Git operation timed out", "output": ""}
    except FileNotFoundError:
        return {"success": False, "updated": False, "message": "Git not found", "output": ""}
    except Exception as e:
        LOG.exception("Git pull error")
        return {"success": False, "updated": False, "message": str(e), "output": ""}
```

- [ ] **Add API endpoint in http_api.py**

Add after the `POST /api/gateway/reset` route in `bridge_python/app/http_api.py`:

```python
@app.post("/api/gateway/git-pull")
async def git_pull_endpoint():
    from app.git_pull import git_pull as do_git_pull
    result = do_git_pull()
    return JSONResponse(result, status_code=200 if result["success"] else 500)
```

- [ ] **Write test for the API endpoint**

Add to `bridge_python/tests/test_http_api.py`:

```python
async def test_git_pull_endpoint(self, client):
    resp = await client.post("/api/gateway/git-pull")
    # Should work or fail gracefully (no git repo in test env)
    assert resp.status_code in (200, 500)
    data = resp.json()
    assert "success" in data
    assert "message" in data
```

- [ ] **Run tests**

Run: `cd bridge_python && python -m pytest tests/test_git_pull.py tests/test_http_api.py::TestHttpAPI::test_git_pull_endpoint -v`
Expected: PASS

- [ ] **Commit Task 6**

```bash
git add bridge_python/app/git_pull.py bridge_python/app/http_api.py bridge_python/tests/test_git_pull.py bridge_python/tests/test_http_api.py
git commit -m "feat: add git pull logic and API endpoint"
```

---

### Task 7: MQTT Force Update Handler + Restart Logic

**Files:**
- Modify: `bridge_python/app/main.py`

- [ ] **Write test for force update MQTT result publishing**

Add to `bridge_python/tests/test_mqtt_discovery.py`:

```python
@pytest.mark.asyncio
async def test_publish_force_update_result_success(self, connected_mqtt):
    await connected_mqtt.publish_force_update_result(True, "Updated with 3 commits")
    connected_mqtt._client.publish.assert_called_once()
    _, kwargs = connected_mqtt._client.publish.call_args
    assert kwargs["topic"] == "esp32-bridge/force_update/result"
    payload = json.loads(kwargs["payload"])
    assert payload["success"] is True
    assert payload["message"] == "Updated with 3 commits"

@pytest.mark.asyncio
async def test_publish_force_update_result_error(self, connected_mqtt):
    await connected_mqtt.publish_force_update_result(False, "Git not found")
    _, kwargs = connected_mqtt._client.publish.call_args
    payload = json.loads(kwargs["payload"])
    assert payload["success"] is False
```

- [ ] **Add `import os` to main.py imports**

Edit `bridge_python/app/main.py`: add `import os` to the imports section.

- [ ] **Implement `handle_force_update` in main.py**

Add to `bridge_python/app/main.py`:

```python
async def handle_force_update():
    from app.git_pull import git_pull as do_git_pull
    result = do_git_pull()
    success = result["success"]
    message = result["message"]
    updated = result.get("updated", False)
    LOG.info("Force update result: success=%s updated=%s message=%s", success, updated, message)
    await mqtt.publish_force_update_result(success, message)
    if success and updated:
        LOG.info("Code updated, restarting in 1s...")
        await asyncio.sleep(1)
        os._exit(0)
```

- [ ] **Wire handle_force_update in force_update_listener**

Edit `bridge_python/app/main.py`: ensure the listener calls `handle_force_update`:

```python
async def force_update_listener():
    if not mqtt or not mqtt._connected:
        return
    try:
        async with mqtt._client.messages() as messages:
            await mqtt._client.subscribe("esp32-bridge/force_update/set")
            async for message in messages:
                if message.topic.value == "esp32-bridge/force_update/set":
                    payload = message.payload.decode()
                    if payload == "PRESS":
                        LOG.info("Force update triggered via MQTT")
                        asyncio.create_task(handle_force_update())
    except Exception:
        LOG.exception("Force update listener error")
```

- [ ] **Run tests**

Run: `cd bridge_python && python -m pytest tests/test_mqtt_discovery.py::TestMQTTDiscovery::test_publish_force_update_result_success tests/test_mqtt_discovery.py::TestMQTTDiscovery::test_publish_force_update_result_error -v`
Expected: PASS

- [ ] **Commit Task 7**

```bash
git add bridge_python/app/main.py bridge_python/tests/test_mqtt_discovery.py
git commit -m "feat: add MQTT force update handler with restart"
```

---

### Task 8: Dashboard Button (HTML admin menu)

**Files:**
- Modify: `bridge_python/app/web/dashboard.html`

- [ ] **Add "Force Update" option to admin menu**

Edit `bridge_python/app/web/dashboard.html`: find the admin dropdown menu (gear icon) and add a line:

```html
<li onclick="forceGitPull()">Forçar Atualização (git pull)</li>
```

Before the `</ul>` of the admin dropdown.

- [ ] **Add confirmation dialog and JS function**

Add to the JavaScript section:

```javascript
async function forceGitPull() {
    if (!confirm("Forçar atualização via git pull? O bridge será reiniciado se houver alterações.")) return;
    showToast("info", "Executando git pull...");
    try {
        const resp = await fetch("/api/gateway/git-pull", { method: "POST" });
        const data = await resp.json();
        if (data.success) {
            if (data.updated) {
                showToast("success", "Atualizado! " + data.message + " Reiniciando...");
            } else {
                showToast("info", "Já está atualizado: " + data.message);
            }
        } else {
            showToast("error", "Falha: " + data.message);
        }
    } catch (err) {
        showToast("error", "Erro de conexão: " + err.message);
    }
}
```

- [ ] **Commit Task 8**

```bash
git add bridge_python/app/web/dashboard.html
git commit -m "feat: add force update button to dashboard admin menu"
```

---

### Task 9: Remove force_update config on shutdown

**Files:**
- Modify: `bridge_python/app/main.py`

- [ ] **Add shutdown handler to clean up MQTT config**

Edit `bridge_python/app/main.py`:

```python
@app.on_event("shutdown")
async def on_shutdown():
    await mqtt.remove_force_update_config()
```

- [ ] **Commit Task 9**

```bash
git add bridge_python/app/main.py
git commit -m "fix: cleanup MQTT force update config on shutdown"
```

---

### Task 10: Full Integration Test

- [ ] **Run all tests**

Run: `cd bridge_python && python -m pytest tests/ -v`
Expected: All tests pass

- [ ] **Verify no regressions**

Run: `cd bridge_python && python -m pytest tests/ --tb=short -q`
Expected: All passing

- [ ] **Commit any test fixes**

```bash
git add -A
git commit -m "test: add git pull button tests, all passing"
```
