@echo off
setlocal

REM ============================================================================
REM TinyIM Development Environment Cleanup Script
REM ============================================================================

echo.
echo [INFO] Stopping TinyIM Development Environment and CLEANING DATA...
echo [WARN] This will DELETE ALL MySQL and Redis data volumes!
echo.

REM Ensure we are in the correct directory for relative paths in docker-compose
if exist "infra\docker" (
    pushd infra\docker
) else (
    REM Fallback if executed from inside scripts folder
    if exist "..\docker" (
        pushd ..\docker
    ) else (
        echo [ERROR] Could not find infra\docker directory.
        pause
        exit /b 1
    )
)

REM Use -v to remove volumes
docker-compose down -v
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to stop services.
    popd
    pause
    exit /b 1
)
popd

echo.
echo [SUCCESS] Environment stopped and data volumes cleaned.
echo [INFO] Next setup_dev will start with a fresh database.
echo.
pause
exit /b 0
