#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <algorithm>
#include "redis_client.h"
#include "config.h"
#include <shared_mutex>
#include <mutex>
#include <spdlog/spdlog.h>
#include <ctime>

class ServiceDiscovery {
public:
    static ServiceDiscovery& GetInstance() {
        static ServiceDiscovery instance;
        return instance;
    }

    void Start() {
        if (running_) return; // Already started
        running_ = true;
        
        // Initial fetch
        UpdateGateways();
        
        // Start background thread
        refresh_thread_ = std::thread([this]() {
            int interval = Config::GetInstance().GetInt("service_discovery.refresh_interval_ms", 3000);
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(interval));
                UpdateGateways();
            }
        });
        spdlog::info("ServiceDiscovery started. Polling every {}ms", Config::GetInstance().GetInt("service_discovery.refresh_interval_ms", 3000));
    }

    void Stop() {
        running_ = false;
        if (refresh_thread_.joinable()) refresh_thread_.join();
    }

    std::string GetRandomGateway() {
        // Zero IO Read from Local Cache
        std::lock_guard<std::shared_mutex> lock(rw_mtx_); // Read Lock
        if (gateway_list_.empty()) return "";
        
        // Fix: static mt19937 is not thread-safe! Use thread_local.
        static thread_local std::mt19937 gen(static_cast<unsigned int>(std::time(nullptr) ^ std::hash<std::thread::id>{}(std::this_thread::get_id())));
        std::uniform_int_distribution<> dis(0, (int)gateway_list_.size() - 1);
        
        // Debug
        // std::cerr << "[DEBUG] GetRandomGateway: Count=" << gateway_list_.size() << std::endl;

        return gateway_list_[dis(gen)];
    }
    
    // For debugging
    size_t GetGatewayCount() {
        std::shared_lock<std::shared_mutex> lock(rw_mtx_);
        return gateway_list_.size();
    }

private:
    ServiceDiscovery() : running_(false) {}
    ~ServiceDiscovery() { Stop(); }

    void UpdateGateways() {
        std::vector<std::string> keys;
        
        // Use SCAN to avoid blocking Redis if keys are many (though gateways won't be millions)
        // But RedisClient::Keys uses "KEYS" command which is O(N).
        // Best practice is SCAN. Let's stick with Keys for MVP as per RedisClient impl, 
        // OR implement Scan in RedisClient.
        // Given RedisClient only has Keys(), we use it for now.
        // Optimization: In real prod, modify RedisClient to support Scan.
        
        try {
            // Find all registered gateways: im:service:gateway:{ip:port} -> "ip:port"
            auto result = RedisClient::GetInstance().Keys("im:service:gateway:*");
            
            std::vector<std::string> new_list;
            for (const auto& key : result) {
                // Key format: im:gateway:127.0.0.1:8081
                // We want the value? Value might be the load or just "1".
                // Actually, if we just need the address, we can parse the key OR get the value if value is the public address.
                // Let's assume Value is the Public Address (User-facing).
                std::string addr = RedisClient::GetInstance().Get(key);
                if (!addr.empty()) {
                    new_list.push_back(addr);
                }
            }
            
            if (!new_list.empty()) {
                std::unique_lock<std::shared_mutex> lock(rw_mtx_); // Write Lock
                gateway_list_ = new_list;
                // spdlog::debug("Refreshed Gateways: {}", gateway_list_.size());
            } else {
                // Don't clear if fetch failed? Or should we?
                // If redis returns empty, it means no gateways are online.
                // We should reflect that.
                 std::unique_lock<std::shared_mutex> lock(rw_mtx_);
                 gateway_list_.clear();
            }
            
        } catch (const std::exception& e) {
            spdlog::error("UpdateGateways failed: {}", e.what());
        }
    }

    std::shared_mutex rw_mtx_;
    std::vector<std::string> gateway_list_;
    std::atomic<bool> running_;
    std::thread refresh_thread_;
};
