from __future__ import annotations
from app.http_api import create_app
from app.device_registry import DeviceRegistry

registry = DeviceRegistry()
app = create_app(registry)


def main() -> None:
    import uvicorn
    from app.config import settings
    registry.load()
    uvicorn.run(app, host="0.0.0.0", port=settings.http_port, log_level=settings.log_level)


if __name__ == "__main__":
    main()
