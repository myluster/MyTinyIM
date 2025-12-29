#!/bin/bash

echo "[INFO] Stopping TinyIM Services..."

# Define service names
SERVICES=("auth_server" "chat_server" "gateway_server")

for service in "${SERVICES[@]}"; do
    if pgrep -f "$service" > /dev/null; then
        echo "   -> Stopping $service..."
        pkill -f "$service"
    fi
done

# Wait for them to actually exit
sleep 1

# Force kill if still running
for service in "${SERVICES[@]}"; do
    if pgrep -f "$service" > /dev/null; then
        echo "   -> Force killing $service..."
        pkill -9 -f "$service"
    fi
done

echo "[SUCCESS] All services stopped."
