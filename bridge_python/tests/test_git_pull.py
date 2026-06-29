from __future__ import annotations
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
        assert result["updated"] is False
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
