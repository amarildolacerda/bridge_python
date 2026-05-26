@echo off
docker inspect -f "{{.State.Running}}" esp-matter-dev 2>nul | find "true" >nul
if %errorlevel% neq 0 (
    echo Container not running. Starting...
    docker start esp-matter-dev
)
docker exec -it esp-matter-dev /bin/bash -c "cd /project && source config.sh && exec /bin/bash"
