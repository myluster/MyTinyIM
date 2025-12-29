@echo off
setlocal EnableDelayedExpansion

REM ============================================================================
REM TinyIM Development Environment Setup & Startup Script (Robust Version)
REM ============================================================================

REM --- Configuration ---
set "COMPOSE_FILE=..\docker\docker-compose.yml"
set "MAX_RETRIES=100"  REM Increase wait time to ~3 minutes
set "RETRY_INTERVAL=2"

echo.
echo [INFO] Starting TinyIM Development Environment...
echo.

REM 1. Start Docker Compose
echo [STEP 1/2] Building and Starting Containers...
pushd infra\docker
docker-compose up -d --build
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to execute docker-compose.
    popd
    pause
    exit /b 1
)
popd

REM 2. Wait for MySQL Replication Setup
echo.
echo [STEP 2/2] Waiting for Database Setup (This may take 1-2 minutes)...
echo [WAIT] Polling 'tinyim_setup' container status...

set "setup_finished=false"
for /L %%i in (1,1,%MAX_RETRIES%) do (
    set "exit_code="
    set "status="
    for /f "tokens=*" %%a in ('docker inspect tinyim_setup --format "{{.State.ExitCode}}" 2^>nul') do set "exit_code=%%a"
    for /f "tokens=*" %%b in ('docker inspect tinyim_setup --format "{{.State.Status}}" 2^>nul') do set "status=%%b"
    
    if "!status!"=="exited" (
        if "!exit_code!"=="0" (
            goto HEALTH_SUCCESS
        ) else (
            echo.
            echo [ERROR] Setup Script failed! (Exit Code: !exit_code!)
            echo [HINT]  Check logs: docker logs tinyim_setup
            rem Don't exit immediately, let user decide
            goto END
        )
    )
    
    echo | set /p=.
    timeout /t %RETRY_INTERVAL% >nul
)

echo.
echo [WARNING] Setup takes longer than expected. It might still be running.
echo [HINT]  Check logs manually: docker logs tinyim_setup
goto END

:HEALTH_SUCCESS
echo.
echo.
echo [SUCCESS] Database Configured Successfully!
echo -----------------------------------------------------------
docker logs tinyim_setup
echo -----------------------------------------------------------
echo.

:END
echo Environment is ready (or attempting to cover).
echo You can now use 'enter_dev.bat' to start coding.
pause
exit /b 0
