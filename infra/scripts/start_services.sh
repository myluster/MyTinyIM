#!/bin/bash

# Configuration
BUILD_DIR="/app/build"
LOG_DIR="/app/logs"
mkdir -p "$LOG_DIR"

# Colors (Optional, but helpful)
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_err() { echo -e "${RED}[ERROR]${NC} $1"; }

# 1. Cleanup Old Services
log_info "Cleaning up old processes..."
SDIR=$(dirname "$(readlink -f "$0")")
if [ -f "$SDIR/stop_services.sh" ]; then
    bash "$SDIR/stop_services.sh"
else
    pkill -f "auth_server|chat_server|user_server|dispatch_server|gateway_server" || true
fi

# 2. Dependency Check (Critical for reliability)
wait_for_service() {
    local host=$1
    local port=$2
    local service=$3
    log_info "Waiting for $service ($host:$port)..."
    for i in {1..30}; do
        if (echo > /dev/tcp/$host/$port) >/dev/null 2>&1; then
            log_info "$service is UP!"
            return 0
        fi
        sleep 1
    done
    log_err "Timeout waiting for $service! Please check if Redis/MySQL containers are running."
    return 1
}

wait_for_service "mysql-master" 3306 "MySQL Master" || exit 1
wait_for_service "tinyim_redis" 6379 "Redis" || exit 1

log_info "Starting Microservices..."

# 3. Start Function
start_service() {
    local dir_name=$1
    local bin_name=$2
    local args=$3
    local bin_path="$BUILD_DIR/src/$dir_name/$bin_name"
    local log_file="$LOG_DIR/${bin_name}${args:+_$args}.log"

    if [ ! -f "$bin_path" ]; then
        log_err "Binary not found: $bin_path"
        log_err "Please run 'cd /app/build && cmake .. && make' first."
        return 1
    fi

    echo -n "   -> Starting $bin_name $args..."
    nohup "$bin_path" $args > "$log_file" 2>&1 &
    
    local pid=$!
    sleep 0.5
    if ! kill -0 $pid 2>/dev/null; then
        echo -e " ${RED}FAILED${NC}"
        tail -n 5 "$log_file"
        return 1
    else
        echo -e " ${GREEN}OK${NC} (Log: $log_file)"
        return 0
    fi
}

# Start Core Services
start_service "auth_server" "auth_server" || exit 1
start_service "chat_server" "chat_server" || exit 1
start_service "user_server" "user_server" || exit 1
start_service "dispatch" "dispatch_server" || exit 1

# Wait for internal gRPCs
sleep 1

# Start Gateways
log_info "Starting Gateways..."
start_service "gateway" "gateway_server" "9080" || exit 1
start_service "gateway" "gateway_server" "9081" || exit 1

# 4. Final Verification
log_info "Verifying Ports..."
failed=0
if ! (echo > /dev/tcp/127.0.0.1/9080) >/dev/null 2>&1; then log_err "Port 9080 unreachable"; failed=1; fi
if ! (echo > /dev/tcp/127.0.0.1/8000) >/dev/null 2>&1; then log_err "Port 8000 unreachable"; failed=1; fi

if [ $failed -eq 0 ]; then
    log_info "SUCCESS: All Services Started & Verified! 🚀"
else
    log_err "Some services failed to bind ports."
fi
