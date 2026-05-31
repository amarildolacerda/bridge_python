@echo off
setlocal enabledelayedexpansion

set CONTAINER_NAME=homeassistant
set IMAGE=ghcr.io/home-assistant/home-assistant:stable
set CONFIG_DIR=%~dp0homeassistant_config

echo ========================================
echo  Home Assistant Launcher
echo ========================================

if not exist "%CONFIG_DIR%" (
    mkdir "%CONFIG_DIR%"
    echo Criado diretorio de config: %CONFIG_DIR%
)

docker ps -a --filter "name=^/%CONTAINER_NAME%$" --format "{{.Names}}" > "%TEMP%\ha_check.txt"
set /p CONTAINER_EXISTS=<"%TEMP%\ha_check.txt"

if "%CONTAINER_EXISTS%"=="" (
    echo Container nao encontrado. Criando...
    docker run -d ^
        --name %CONTAINER_NAME% ^
        --restart unless-stopped ^
        -p 8123:8123 ^
        -v "%CONFIG_DIR%:/config" ^
        -v /etc/localtime:/etc/localtime:ro ^
        %IMAGE% > "%TEMP%\ha_create.txt" 2>&1
    set CREATE_EXIT=%errorlevel%
    type "%TEMP%\ha_create.txt"
    docker ps -a --filter "name=^/%CONTAINER_NAME%$" --format "{{.Names}}" > "%TEMP%\ha_check2.txt"
    set /p CONFIRMED=<"%TEMP%\ha_check2.txt"
    if "%CONFIRMED%"=="" (
        echo ERRO: Container nao foi criado.
        exit /b 1
    )
    call :wait_ready
) else (
    docker inspect -f "{{.State.Status}}" %CONTAINER_NAME% > "%TEMP%\ha_state.txt" 2>nul
    set /p STATE=<"%TEMP%\ha_state.txt"
    if "!STATE!" neq "running" (
        echo Container parado. Iniciando...
        docker start %CONTAINER_NAME%
        call :wait_ready
    ) else (
        echo Container ja esta rodando.
    )
)

echo.
echo Home Assistant em http://localhost:8123
echo.
call :wait_healthy
echo.
echo Home Assistant pronto! Acesse: http://localhost:8123
exit /b 0

:wait_ready
echo Aguardando container ficar pronto...
set WAIT_COUNT=0
:loop
set /a WAIT_COUNT+=1
if !WAIT_COUNT! gtr 60 (
    echo ERRO: Container nao iniciou apos 60 segundos.
    echo Verifique com: docker logs %CONTAINER_NAME%
    exit /b 1
)
docker inspect -f "{{.State.Status}}" %CONTAINER_NAME% > "%TEMP%\ha_state.txt" 2>nul
set /p STATE=<"%TEMP%\ha_state.txt"
if "!STATE!"=="running" (
    timeout /t 2 /nobreak >nul
    exit /b 0
)
timeout /t 1 /nobreak >nul
goto loop

:wait_healthy
echo Aguardando Home Assistant responder HTTP...
set WAIT_COUNT=0
:health_loop
set /a WAIT_COUNT+=1
if !WAIT_COUNT! gtr 120 (
    echo Home Assistant nao respondeu apos 120s.
    echo Verifique os logs: docker logs %CONTAINER_NAME%
    exit /b 1
)
docker exec %CONTAINER_NAME% curl -s -o /dev/null -w "%%{http_code}" http://127.0.0.1:8123 > "%TEMP%\ha_http.txt" 2>nul
set /p HTTP_CODE=<"%TEMP%\ha_http.txt"
if "!HTTP_CODE!"=="200" (
    echo Home Assistant pronto (!WAIT_COUNT!s)
    exit /b 0
)
timeout /t 1 /nobreak >nul
goto health_loop
