#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <mutex>
#include <memory>
#include <spdlog/spdlog.h>

class RedisClient {
public:
    static RedisClient& GetInstance();

    void Init(const std::string& host, int port);
    
    // Basic operations
    bool Set(const std::string& key, const std::string& value);
    std::string Get(const std::string& key);
    bool Del(const std::string& key);
    bool Exists(const std::string& key);
    bool Expire(const std::string& key, int seconds);
    bool SetEx(const std::string& key, const std::string& value, int seconds);
    std::vector<std::string> Keys(const std::string& pattern);

    // Hash operations
    bool HSet(const std::string& key, const std::string& field, const std::string& value);
    std::string HGet(const std::string& key, const std::string& field);
    bool HDel(const std::string& key, const std::string& field);
    std::unordered_map<std::string, std::string> HGetAll(const std::string& key);

    // Pub/Sub
    bool Publish(const std::string& channel, const std::string& message);
    // Note: Subscribe blocks the thread. Callback will be called on message.
    void Subscribe(const std::string& channel, std::function<void(const std::string& msg)> callback);

    // List operations (for offline msgs queue if needed, though we use DB for persistence)
    // Using Redis for Online Status mainly. Key: "user_status:<uid>" -> "server_id" or "online"

    redisContext* GetContext(); // For advanced raw usage
    void ReleaseContext(redisContext* ctx); // If we implemented a pool, but for now single or thread-local?
    // Hiredis context is not thread-safe. We need a pool or thread-local.
    // Simplifying: Simple pool logic similar to DB, or just short-lived connections for low concurrency dev, 
    // OR one connection per thread. Using a mutex-protected single connection is SLOW but safe.
    // Let's implement a simple pool of contexts.

private:
    RedisClient() = default;
    ~RedisClient();
    
    std::string host_;
    int port_;
    
    // Simple pool
    std::mutex mtx_;
    std::vector<redisContext*> pool_;
};

// RAII
class RedisConn {
public:
    RedisConn() { ctx_ = RedisClient::GetInstance().GetContext(); }
    ~RedisConn() { if (ctx_) RedisClient::GetInstance().ReleaseContext(ctx_); }
    redisContext* get() { return ctx_; }
private:
    redisContext* ctx_;
};
