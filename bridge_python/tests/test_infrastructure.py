from __future__ import annotations
import os
import yaml


PROJECT_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))


def test_config_has_watchdog():
    with open(os.path.join(PROJECT_ROOT, "config.yaml")) as f:
        cfg = yaml.safe_load(f)
    assert cfg.get("watchdog") is True, "config.yaml must have watchdog: true"


def test_config_has_addons_map():
    with open(os.path.join(PROJECT_ROOT, "config.yaml")) as f:
        cfg = yaml.safe_load(f)
    assert "map" in cfg, "config.yaml must have a map key"
    assert "addons:rw" in cfg["map"], "config.yaml map must include addons:rw"


def test_dockerfile_installs_git():
    dockerfile = os.path.join(PROJECT_ROOT, "Dockerfile")
    with open(dockerfile) as f:
        content = f.read()
    assert "apt-get install" in content, "Dockerfile must run apt-get install"
    assert " git" in content, "Dockerfile must install git"


def test_dockerfile_cmd_uses_run_sh():
    dockerfile = os.path.join(PROJECT_ROOT, "Dockerfile")
    with open(dockerfile) as f:
        content = f.read()
    assert 'exec /app/run.sh' in content, "Dockerfile CMD must exec /app/run.sh"
