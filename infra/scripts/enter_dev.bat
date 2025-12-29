@echo off
setlocal EnableDelayedExpansion

REM 定义颜色
set "GREEN=[32m"
set "RED=[31m"
set "YELLOW=[33m"
set "RESET=[0m"
REM 注意: Windows CMD 中直接输 Esc 字符比较麻烦，这里简化处理，只做逻辑增强

echo Checking development container status...

REM 获取容器状态
set "CONTAINER_STATUS="
for /f "tokens=*" %%i in ('docker inspect -f "{{.State.Status}}" tinyim_dev 2^>nul') do set CONTAINER_STATUS=%%i

if "%CONTAINER_STATUS%"=="" (
    echo [ERROR] Container 'tinyim_dev' not found.
    echo Please run 'docker-compose up -d' in infra/docker first.
    pause
    exit /b 1
)

if "%CONTAINER_STATUS%"=="running" (
    echo [INFO] Container is running. Entering shell...
) else (
    echo [WARN] Container is in state: %CONTAINER_STATUS%.
    echo Attempting to start container...
    docker start tinyim_dev
    if errorlevel 1 (
        echo [ERROR] Failed to start container.
        pause
        exit /b 1
    )
    echo [INFO] Container started.
)

REM 进入容器
docker exec -it tinyim_dev bash
