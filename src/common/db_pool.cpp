#include "db_pool.h"
#include <spdlog/spdlog.h>
#include <random>

DBPool& DBPool::GetInstance() {
    static DBPool instance;
    return instance;
}

DBPool::~DBPool() {
    // Cleanup Master
    {
        std::lock_guard<std::mutex> lock(master_mtx_);
        while (!master_queue_.empty()) {
            MYSQL* conn = master_queue_.front();
            master_queue_.pop();
            mysql_close(conn);
        }
    }
    // Cleanup Slave
    {
        std::lock_guard<std::mutex> lock(slave_mtx_);
        while (!slave_queue_.empty()) {
            MYSQL* conn = slave_queue_.front();
            slave_queue_.pop();
            mysql_close(conn);
        }
    }
}

// Single Host Init (Treat as Master only)
void DBPool::Init(const std::string& host, int port, const std::string& user, 
                  const std::string& password, const std::string& dbname, int max_conns) {
    Init(host, {}, port, user, password, dbname, max_conns);
}

// Master-Slave Init
void DBPool::Init(const std::string& master_host, const std::vector<std::string>& slave_hosts, 
                  int port, const std::string& user, const std::string& password, 
                  const std::string& dbname, int max_conns) {
    master_host_ = master_host;
    slave_hosts_ = slave_hosts;
    port_ = port;
    user_ = user;
    password_ = password;
    dbname_ = dbname;
    max_conns_ = max_conns;

    // Pre-create Master Connections (Min 2)
    for (int i = 0; i < 2; ++i) {
        MYSQL* conn = CreateConnection(master_host_);
        if (conn) {
            master_queue_.push(conn);
            master_active_count_++;
        }
    }

    // Pre-create Slave Connections (If any slaves exist)
    if (!slave_hosts_.empty()) {
        // Create at least one connection per slave host, up to min 2 total
        for (const auto& host : slave_hosts_) {
            MYSQL* conn = CreateConnection(host);
            if (conn) {
                slave_queue_.push(conn);
                slave_active_count_++;
            }
        }
    } else {
        // No slaves? Just use Master for reads too?
        // Our GetReadConnection will gracefully fallback to master if slave_hosts is empty.
    }
    
    spdlog::info("DBPool Initialized. Master: {}, Slaves: {}", master_host_, slave_hosts_.size());
}

MYSQL* DBPool::CreateConnection(const std::string& host) {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        spdlog::error("MySQL init failed");
        return nullptr;
    }
    
    // Set timeout options
    int timeout = 3;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (!mysql_real_connect(conn, host.c_str(), user_.c_str(), password_.c_str(), dbname_.c_str(), port_, nullptr, 0)) {
        spdlog::error("MySQL connect failed to {}: {}", host, mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

std::shared_ptr<MYSQL> DBPool::GetWriteConnection() {
    std::unique_lock<std::mutex> lock(master_mtx_);
    
    // Lazy create
    if (master_queue_.empty() && master_active_count_ < max_conns_) {
        MYSQL* conn = CreateConnection(master_host_);
        if (conn) {
            master_active_count_++;
            // Return directly
            return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) { ReleaseMasterConnection(c); });
        }
    }

    // Wait
    if(master_queue_.empty()) {
         if (master_cv_.wait_for(lock, std::chrono::seconds(3), [this] { return !master_queue_.empty(); }) == false) {
             spdlog::error("DBPool Master Timeout");
             return nullptr;
         }
    }

    MYSQL* conn = master_queue_.front();
    master_queue_.pop();

    // Ping check
    if (mysql_ping(conn) != 0) {
        spdlog::warn("MySQL Master lost, reconnecting...");
        mysql_close(conn);
        conn = CreateConnection(master_host_);
        if (!conn) {
             master_active_count_--; // Decrement since we failed to replace
             return nullptr;
        }
    }

    return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) { ReleaseMasterConnection(c); });
}

std::shared_ptr<MYSQL> DBPool::GetReadConnection() {
    if (slave_hosts_.empty()) {
        return GetWriteConnection(); // Fallback to Master
    }

    std::unique_lock<std::mutex> lock(slave_mtx_);

    // Lazy create
    if (slave_queue_.empty() && slave_active_count_ < max_conns_) {
        // Pick random slave host
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, slave_hosts_.size() - 1);
        std::string host = slave_hosts_[dis(gen)];

        MYSQL* conn = CreateConnection(host);
        if (conn) {
            slave_active_count_++;
            return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) { ReleaseSlaveConnection(c); });
        }
    }

    // Wait
    if(slave_queue_.empty()) {
         if (slave_cv_.wait_for(lock, std::chrono::seconds(3), [this] { return !slave_queue_.empty(); }) == false) {
             spdlog::error("DBPool Slave Timeout (Fallbacking to Master)");
             return GetWriteConnection(); // Fallback
         }
    }

    MYSQL* conn = slave_queue_.front();
    slave_queue_.pop();

    // Ping check
    if (mysql_ping(conn) != 0) {
        spdlog::warn("MySQL Slave lost, reconnecting...");
        mysql_close(conn);
        // Reconnect to a random slave
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, slave_hosts_.size() - 1);
        std::string host = slave_hosts_[dis(gen)];
        
        conn = CreateConnection(host);
        if (!conn) {
             slave_active_count_--;
             return nullptr;
        }
    }

    return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) { ReleaseSlaveConnection(c); });
}

std::shared_ptr<MYSQL> DBPool::GetConnection() {
    return GetWriteConnection();
}

void DBPool::ReleaseMasterConnection(MYSQL* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(master_mtx_);
    master_queue_.push(conn);
    master_cv_.notify_one();
}

void DBPool::ReleaseSlaveConnection(MYSQL* conn) {
    if (!conn) return;
    std::lock_guard<std::mutex> lock(slave_mtx_);
    slave_queue_.push(conn);
    slave_cv_.notify_one();
}
