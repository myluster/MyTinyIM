#!/bin/bash

# Config
BUILD_DIR="/app/build"
LOG_DIR="/app/logs"
mkdir -p "$LOG_DIR"

# Ensure we are in a clean state
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
bash "$SCRIPT_DIR/stop_services.sh"

echo "[INFO] Starting TinyIM Services..."

# Helper function
start_service() {
    local dir_name=$1
    local bin_name=$2
    local args=$3
    local bin_path="$BUILD_DIR/src/$dir_name/$bin_name"
    local log_file="$LOG_DIR/$bin_name.log"

    # If args is defined, append to log name to allow multiple instances
    if [ ! -z "$args" ]; then
        log_file="$LOG_DIR/${bin_name}_${args}.log"
    fi

    if [ ! -f "$bin_path" ]; then
        echo "[ERROR] Binary not found: $bin_path"
        echo "        Please run 'cmake .. && make' in /app/build first."
        exit 1
    fi

    echo "   -> Starting $bin_name $args (Logs: $log_file)..."
    nohup "$bin_path" $args > "$log_file" 2>&1 &
    
    # Simple check if process started
    local pid=$!
    sleep 0.5
    if ! kill -0 $pid 2>/dev/null; then
        echo "[ERROR] $bin_name failed to start! Check logs:"
        tail -n 5 "$log_file"
        return 1
    fi
    return 0
}

# Start all
# Format: start_service "dir_name_in_src" "binary_name" "args"
start_service "auth_server" "auth_server" || exit 1
start_service "chat_server" "chat_server" || exit 1
sleep 1 # Wait for internal gRPCs if needed

# Start Multiple Gateways
start_service "gateway" "gateway_server" "8080" || exit 1
start_service "gateway" "gateway_server" "8081" || exit 1

echo "[INFO] Waiting for ports..."

echo "[INFO] Waiting for ports..."
sleep 2

# Check ports (netstat might not be installed, use simple log check or ps)
if pgrep -f "gateway_server" > /dev/null; then
    echo "[SUCCESS] Services are UP!"
    echo "   - Auth Server:    Listening..."
    echo "   - Chat Server:    Listening..."
    echo "   - Gateway Server: Port 8080"
else
    echo "[WARN] Gateway server does not seem to be running."
fi

echo ""
echo "Tail logs with:"
echo "   tail -f logs/gateway_server.log"
