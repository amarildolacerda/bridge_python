@echo off
for /f %%i in ('docker container ls -a --filter name=esp-matter-dev --format {{.Names}} 2^>nul') do set CONTAINER_EXISTS=%%i

if "%CONTAINER_EXISTS%"=="" (
    echo Container esp-matter-dev not found. Creating with docker compose...
    docker compose -f "%~dp0docker-compose.yml" up -d esp-matter-dev
    if %errorlevel% neq 0 exit /b %errorlevel%
    call :wait_ready
) else (
    docker inspect -f "{{.State.Running}}" esp-matter-dev 2>nul | find "true" >nul
    if %errorlevel% neq 0 (
        echo Container not running. Starting...
        docker start esp-matter-dev
        if %errorlevel% neq 0 exit /b %errorlevel%
        call :wait_ready
    )
)

docker exec esp-matter-dev /bin/bash -c "cd /project && source config.sh && idf.py build"
echo Build completed.
exit /b %errorlevel%

:wait_ready
echo Waiting for container to be ready...
:loop
docker inspect -f "{{.State.Running}}" esp-matter-dev 2>nul | find "true" >nul
if %errorlevel% equ 0 (
    timeout /t 1 /nobreak >nul
    exit /b 0
)
timeout /t 1 /nobreak >nul
goto loop
