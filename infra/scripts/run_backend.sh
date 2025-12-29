#!/bin/bash
set -e

# Colors
GREEN='\033[0;32m'
RESET='\033[0m'

echo -e "${GREEN}[Build] Creating build directory...${RESET}"
mkdir -p build
cd build

echo -e "${GREEN}[Build] Running CMake...${RESET}"
cmake ..

echo -e "${GREEN}[Build] compiling...${RESET}"
make -j$(nproc)

echo -e "${GREEN}[Run] Starting Auth Server...${RESET}"
./src/auth_server/auth_server &
AUTH_PID=$!

echo -e "${GREEN}[Run] Starting Chat Server...${RESET}"
./src/chat_server/chat_server &
CHAT_PID=$!

echo -e "${GREEN}[Run] Starting Gateway Server...${RESET}"
./src/gateway/gateway_server &
GATEWAY_PID=$!

echo -e "${GREEN}[Ready] Services are running in background.${RESET}"
echo "Auth Server PID: $AUTH_PID"
echo "Chat Server PID: $CHAT_PID"
echo "Gateway Server PID: $GATEWAY_PID"

# Wait for processes
wait
