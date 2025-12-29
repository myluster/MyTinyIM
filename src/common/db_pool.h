#pragma once

#include <mysql/mysql.h>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <spdlog/spdlog.h>

class DBPool {
public:
    static DBPool& GetInstance();

    void Init(const std::string& master_host, const std::vector<std::string>& slave_hosts, 
              int port, const std::string& user, const std::string& password, 
              const std::string& dbname, int max_conns = 10);

    // Legacy Init for single DB (compatible)
    void Init(const std::string& host, int port, const std::string& user, 
              const std::string& password, const std::string& dbname, int max_conns = 10);
    
    std::shared_ptr<MYSQL> GetWriteConnection();
    std::shared_ptr<MYSQL> GetReadConnection();
    std::shared_ptr<MYSQL> GetConnection(); // Defaults to Write (Master)

private:
    DBPool() = default;
    ~DBPool();
    DBPool(const DBPool&) = delete;
    DBPool& operator=(const DBPool&) = delete;

    // Helper to release correctly
    void ReleaseMasterConnection(MYSQL* conn);
    void ReleaseSlaveConnection(MYSQL* conn);
    
    MYSQL* CreateConnection(const std::string& host);

    // Master Pool
    std::queue<MYSQL*> master_queue_;
    std::mutex master_mtx_;
    std::condition_variable master_cv_;
    int master_active_count_ = 0;

    // Slave Pool
    std::queue<MYSQL*> slave_queue_;
    std::mutex slave_mtx_;
    std::condition_variable slave_cv_;
    int slave_active_count_ = 0;

    // Config
    std::string master_host_;
    std::vector<std::string> slave_hosts_;
    int port_;
    std::string user_;
    std::string password_;
    std::string dbname_;
    int max_conns_;
};

// RAII Helper
class DBConn {
public:
    enum Type { READ, WRITE };
    
    DBConn(Type type = WRITE) { 
        if (type == READ) conn_ = DBPool::GetInstance().GetReadConnection();
        else conn_ = DBPool::GetInstance().GetWriteConnection();
    }
    
    // Legacy support
    // DBConn() : DBConn(WRITE) {} 
    
    // No manual release in dtor needed because shared_ptr has custom deleter
    ~DBConn() = default; 
    
    MYSQL* get() { return conn_.get(); }
    bool valid() { return conn_ != nullptr; }

private:
    std::shared_ptr<MYSQL> conn_;
};
