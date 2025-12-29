#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>

class ServiceRegistry {
public:
    static ServiceRegistry& GetInstance();

    // Register a service (Server Side)
    // service_name: e.g. "chat"
    // ip, port: e.g. "127.0.0.1", 50052
    void Register(const std::string& service_name, const std::string& ip, int port);

    // Discover a service (Client Side)
    // Returns "ip:port" string. Empty if not found.
    std::string Discover(const std::string& service_name);

    // Start observing a service type for caching.
    // e.g. Observe("gateway")
    void Observe(const std::string& service_name);

private:
    ServiceRegistry();
    ~ServiceRegistry();

    void HeartbeatLoop();
    void PollingLoop(); // Refreshes observers
    void RefreshCache();

    std::string current_service_name_;
    std::string current_ip_;
    int current_port_ = 0;
    bool is_registered_ = false;

    std::atomic<bool> running_{false};
    std::thread heartbeat_thread_;
    std::thread polling_thread_; // New thread for reading

    // Observed services
    std::vector<std::string> observed_services_;
    std::mutex obs_mtx_;

    // Cache: service_name -> vector<address>
    std::unordered_map<std::string, std::vector<std::string>> cache_;
    std::mutex cache_mtx_;
    
    // Round-robin index: service_name -> index
    std::unordered_map<std::string, int> rr_index_;
};
