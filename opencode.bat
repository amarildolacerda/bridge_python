@echo off
setlocal enabledelayedexpansion

set CONTAINER_NAME=opencode-ai
set LOCAL_PORT=3001

echo ========================================
echo  OpenCode Server Launcher
echo ========================================

docker ps -a --filter "name=^/%CONTAINER_NAME%$" --format "{{.Names}}" > "%TEMP%\oc_check.txt"
set /p CONTAINER_EXISTS=<"%TEMP%\oc_check.txt"

if "%CONTAINER_EXISTS%"=="" (
    echo Container nao encontrado. Iniciando com docker compose...
    docker compose -f "%~dp0docker-compose.yml" up -d opencode
    if %errorlevel% neq 0 (
        echo ERRO ao iniciar servico opencode.
        exit /b %errorlevel%
    )
    call :wait_ready
) else (
    docker inspect -f "{{.State.Status}}" %CONTAINER_NAME% > "%TEMP%\oc_state.txt" 2>nul
    set /p STATE=<"%TEMP%\oc_state.txt"
    if "!STATE!" neq "running" (
        echo Container parado. Iniciando...
        docker start %CONTAINER_NAME%
    ) else (
        echo Container ja esta rodando.
    )
)

echo.
echo Abrindo OpenCode em http://localhost:%LOCAL_PORT%
start http://localhost:%LOCAL_PORT%
exit /b 0

:wait_ready
echo Aguardando OpenCode ficar pronto...
set WAIT_COUNT=0
:loop
set /a WAIT_COUNT+=1
if !WAIT_COUNT! gtr 30 (
    echo ERRO: OpenCode nao iniciou apos 30s.
    exit /b 1
)
docker inspect -f "{{.State.Status}}" %CONTAINER_NAME% > "%TEMP%\oc_state.txt" 2>nul
set /p STATE=<"%TEMP%\oc_state.txt"
if "!STATE!"=="running" (
    timeout /t 2 /nobreak >nul
    exit /b 0
)
timeout /t 1 /nobreak >nul
goto loop
