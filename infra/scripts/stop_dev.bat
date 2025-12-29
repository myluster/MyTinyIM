@echo off
setlocal

REM ============================================================================
REM TinyIM Development Environment Shutdown Script
REM ============================================================================

echo.
echo [INFO] Stopping TinyIM Development Environment...
echo.

REM Ensure we are in the correct directory for relative paths in docker-compose
if exist "infra\docker" (
    pushd infra\docker
) else (
    REM Fallback if executed from inside scripts folder (though standard usage is from root)
    if exist "..\docker" (
        pushd ..\docker
    ) else (
        echo [ERROR] Could not find infra\docker directory.
        pause
        exit /b 1
    )
)

docker-compose down
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to stop services.
    popd
    pause
    exit /b 1
)
popd

echo.
echo [SUCCESS] Environment stopped.
echo [INFO] NOTE: Data volumes (MySQL data) are preserved. 
echo [INFO] To completely reset the database, run: docker-compose down -v
echo.
pause
exit /b 0
