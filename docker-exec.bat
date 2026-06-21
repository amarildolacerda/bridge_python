@echo off
setlocal enabledelayedexpansion

echo Verificando container esp-rainmaker-dev...

docker ps -a --filter "name=^/esp-rainmaker-dev$" --format "{{.Names}}" > "%TEMP%\docker_check.txt"
set /p CONTAINER_NAME=<"%TEMP%\docker_check.txt"

if "%CONTAINER_NAME%"=="" (
    echo Container nao encontrado. Executando docker compose...
    docker compose -f "%~dp0docker-compose.yml" up -d esp-rainmaker-dev
    if %errorlevel% neq 0 (
        echo Erro ao criar container.
        exit /b %errorlevel%
    )
    call :wait_ready
) else (
    echo Container encontrado: %CONTAINER_NAME%
    docker inspect -f "{{.State.Status}}" esp-rainmaker-dev > "%TEMP%\docker_state.txt"
    set /p STATE=<"%TEMP%\docker_state.txt"
    if "!STATE!" neq "running" (
        echo Container parado. Iniciando...
        docker start esp-rainmaker-dev
        if %errorlevel% neq 0 (
            echo Erro ao iniciar container.
            exit /b %errorlevel%
        )
        call :wait_ready
    ) else (
        echo Container ja esta rodando.
    )
)

echo.
echo Conectando ao container...
docker exec -it esp-rainmaker-dev /bin/bash -c "cd /project && source config.sh && exec /bin/bash"
exit /b %errorlevel%

:wait_ready
echo Aguardando container ficar pronto...
set WAIT_COUNT=0
:loop
set /a WAIT_COUNT+=1
if !WAIT_COUNT! gtr 30 (
    echo ERRO: Container nao iniciou apos 30 segundos.
    exit /b 1
)
docker inspect -f "{{.State.Status}}" esp-rainmaker-dev > "%TEMP%\docker_state.txt" 2>nul
set /p STATE=<"%TEMP%\docker_state.txt"
if "!STATE!"=="running" (
    echo Container pronto! (!WAIT_COUNT!s)
    timeout /t 2 /nobreak >nul
    exit /b 0
)
timeout /t 1 /nobreak >nul
goto loop
