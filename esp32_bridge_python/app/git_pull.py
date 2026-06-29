from __future__ import annotations
import asyncio
import logging
import os
import subprocess

LOG = logging.getLogger(__name__)


def _find_git_root(path: str) -> str | None:
    current = os.path.abspath(path)
    while True:
        if os.path.isdir(os.path.join(current, ".git")):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            return None
        current = parent


def detect_repo_path() -> str:
    src_dir = os.environ.get("BRIDGE_SRC_DIR", "")
    if src_dir:
        found = _find_git_root(src_dir)
        if found:
            return found
    candidates = [
        "/addons/esp32_bridge_python",
        "/data/bridge_python",
        "/app",
    ]
    for path in candidates:
        found = _find_git_root(path)
        if found:
            return found
    return ""


def _sync_git_pull(repo_path: str) -> dict:
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


async def git_pull(repo_path: str | None = None) -> dict:
    if repo_path is None:
        repo_path = detect_repo_path()
    loop = asyncio.get_event_loop()
    return await loop.run_in_executor(None, _sync_git_pull, repo_path)
