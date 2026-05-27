@echo off
docker inspect esp-matter-dev >nul 2>&1
if %errorlevel% neq 0 (
    echo Container not found. Creating with docker compose...
    docker compose -f "%~dp0docker-compose.yml" up -d esp-matter-dev
    if %errorlevel% neq 0 exit /b %errorlevel%
) else (
    docker inspect -f "{{.State.Running}}" esp-matter-dev 2>nul | find "true" >nul
    if %errorlevel% neq 0 (
        echo Container not running. Starting...
        docker start esp-matter-dev
        if %errorlevel% neq 0 exit /b %errorlevel%
    )
)
docker exec esp-matter-dev /bin/bash -c "cd /project && source config.sh && idf.py build"
if %errorlevel% neq 0 (
    echo Build failed.
    exit /b %errorlevel%
)
echo Build successful.
