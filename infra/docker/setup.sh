#!/bin/bash
# 自动配置 MySQL 一主两从复制 (在 docker-compose 容器 ops-setup 内运行)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

log_info() { echo -e "${CYAN}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[DONE]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

wait_for_mysql() {
    local host="$1"
    local max_retries=120 # Wait up to 2-3 minutes
    log_info "Waiting for MySQL on $host to be ready..."
    
    local count=0
    while [ $count -lt $max_retries ]; do
        if mysql -h "$host" -u"$USER" -p"$PASS" -e "SELECT 1;" >/dev/null 2>&1; then
            log_success "MySQL on $host is accessible."
            return 0
        fi
        echo -n "."
        sleep 2
        ((count++))
    done
    
    log_error "Timeout waiting for MySQL on $host."
    return 1
}

# --- Main Script ---
log_info "Starting replication setup & DB Initialization..."

MASTER_HOST="tinyim_mysql_master"
SLAVE_1_HOST="tinyim_mysql_slave_1"
SLAVE_2_HOST="tinyim_mysql_slave_2"
USER="root"
PASS="root"
REP_USER="repl_user"
REP_PASS="repl_password"

# 1. Wait for Master
wait_for_mysql "$MASTER_HOST" || exit 1

# 2. Ensure Database Schema Exists (Force Init)
# Sometimes auto-init by docker entrypoint is slow or failed. We force it here.
log_info "Ensuring Database Schema setup..."
if mysql -h "$MASTER_HOST" -u"$USER" -p"$PASS" tinyim -e "SELECT 1 FROM im_user LIMIT 1;" >/dev/null 2>&1; then
    log_info "Database 'tinyim' already exists."
else
    log_info "Initializing database from /app/sql/init.sql..."
    # Note: assuming init.sql is mounted at /app/sql/init.sql or similar path inside setup container
    # Looking at docker-compose, setup container mounts ./sql:/app/sql
    if [ -f "/app/sql/init.sql" ]; then
        mysql -h "$MASTER_HOST" -u"$USER" -p"$PASS" < /app/sql/init.sql
        log_success "Database initialized successfully."
    else
        log_error "init.sql not found at /app/sql/init.sql"
    fi
fi

# 3. Create replication user
log_info "Creating replication user on Master..."
mysql -h "$MASTER_HOST" -u"$USER" -p"$PASS" -e "CREATE USER IF NOT EXISTS '$REP_USER'@'%' IDENTIFIED BY '$REP_PASS'; GRANT REPLICATION SLAVE ON *.* TO '$REP_USER'@'%'; FLUSH PRIVILEGES;"
log_success "Replication user ensured."

# 4. Get Master Status
log_info "Getting Master status..."
FILE=$(mysql -h "$MASTER_HOST" -u"$USER" -p"$PASS" -e "SHOW MASTER STATUS\G" | grep 'File:' | awk '{print $2}' | xargs)
POS=$(mysql -h "$MASTER_HOST" -u"$USER" -p"$PASS" -e "SHOW MASTER STATUS\G" | grep 'Position:' | awk '{print $2}' | xargs)

if [ -z "$FILE" ] || [ -z "$POS" ]; then
    log_error "Could not get Master status."
    exit 1
fi
echo "   -> File: $FILE | Position: $POS"

# 5. Configure Slaves
configure_slave() {
    log_info "Configuring Slave: $host"
    wait_for_mysql "$host" || return 1
    
    # Ensure Slave has schema too (Since we sync from POST-init position)
    if [ -f "/app/sql/init.sql" ]; then
        log_info "Initializing Slave schema on $host..."
        mysql -h "$host" -u"$USER" -p"$PASS" < /app/sql/init.sql
    fi

    mysql -h "$host" -u"$USER" -p"$PASS" -e "STOP SLAVE; CHANGE MASTER TO MASTER_HOST='$MASTER_HOST', MASTER_USER='$REP_USER', MASTER_PASSWORD='$REP_PASS', MASTER_LOG_FILE='$FILE', MASTER_LOG_POS=$POS; START SLAVE;"
    log_success "Slave $host configured."
}

configure_slave "$SLAVE_1_HOST"
configure_slave "$SLAVE_2_HOST"

# 6. Health Check
echo ""
echo -e "${YELLOW}=== CLUSTER HEALTH CHECK ===${NC}"
check_slave() {
    local host=$1; local name=$2
    local status=$(mysql -h "$host" -u"$USER" -p"$PASS" -e "SHOW SLAVE STATUS\G")
    local io=$(echo "$status" | grep 'Slave_IO_Running:' | awk '{print $2}')
    local sql=$(echo "$status" | grep 'Slave_SQL_Running:' | awk '{print $2}')
    if [ "$io" == "Yes" ] && [ "$sql" == "Yes" ]; then
        echo -e "$name: ${GREEN}[OK]${NC}"
    else
        echo -e "$name: ${RED}[FAIL]${NC} (IO:$io, SQL:$sql)"
    fi
}
check_slave "$SLAVE_1_HOST" "Slave 1"
check_slave "$SLAVE_2_HOST" "Slave 2"
echo -e "${YELLOW}============================${NC}"
