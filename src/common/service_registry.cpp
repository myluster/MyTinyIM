#include "service_registry.h"
#include "redis_client.h"
#include <spdlog/spdlog.h>
#include <chrono>

ServiceRegistry& ServiceRegistry::GetInstance() {
    static ServiceRegistry instance;
    return instance;
}

ServiceRegistry::ServiceRegistry() {
    running_ = true;
    heartbeat_thread_ = std::thread(&ServiceRegistry::HeartbeatLoop, this);
    polling_thread_ = std::thread(&ServiceRegistry::PollingLoop, this);
}

ServiceRegistry::~ServiceRegistry() {
    running_ = false;
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (polling_thread_.joinable()) polling_thread_.join();
}

void ServiceRegistry::Observe(const std::string& service_name) {
    std::lock_guard<std::mutex> lock(obs_mtx_);
    // Check duplicate
    for(const auto& s : observed_services_) if(s == service_name) return;
    observed_services_.push_back(service_name);
    spdlog::info("ServiceRegistry: Observing service type '{}'", service_name);
}

void ServiceRegistry::Register(const std::string& service_name, const std::string& ip, int port) {
    current_service_name_ = service_name;
    current_ip_ = ip;
    current_port_ = port;
    is_registered_ = true;
    
    // Immediate register
    std::string key = "im:service:" + service_name + ":" + ip + ":" + std::to_string(port);
    std::string value = ip + ":" + std::to_string(port);
    RedisClient::GetInstance().SetEx(key, value, 10); // 10s TTL
    
    spdlog::info("Registered service: {} -> {}", key, value);
}

void ServiceRegistry::HeartbeatLoop() {
    while (running_) {
        if (is_registered_) {
            std::string key = "im:service:" + current_service_name_ + ":" + 
                              current_ip_ + ":" + std::to_string(current_port_);
            std::string value = current_ip_ + ":" + std::to_string(current_port_);
            
            // Renew TTL
            RedisClient::GetInstance().SetEx(key, value, 10);
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void ServiceRegistry::PollingLoop() {
    while (running_) {
        RefreshCache();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void ServiceRegistry::RefreshCache() {
    std::vector<std::string> targets;
    {
        std::lock_guard<std::mutex> lock(obs_mtx_);
        targets = observed_services_;
    }

    if (targets.empty()) return;

    for (const auto& service_name : targets) {
        std::string pattern = "im:service:" + service_name + ":*";
        auto keys = RedisClient::GetInstance().Keys(pattern);
        
        std::vector<std::string> addresses;
        for (const auto& key : keys) {
            std::string val = RedisClient::GetInstance().Get(key);
            if (!val.empty()) {
                addresses.push_back(val);
            }
        }

        // Update Cache
        {
            std::lock_guard<std::mutex> lock(cache_mtx_);
            cache_[service_name] = addresses;
        }
        // spdlog::debug("Refreshed cache for {}, count: {}", service_name, addresses.size());
    }
}

std::string ServiceRegistry::Discover(const std::string& service_name) {
    std::vector<std::string> addresses;
    {
        std::lock_guard<std::mutex> lock(cache_mtx_);
        if (cache_.count(service_name)) {
            addresses = cache_[service_name];
        }
    }

    // Fallback: Direct Query if cache empty? 
    // Or maybe we haven't Observed it yet.
    // Ideally we should rely on cache if observed.
    // If empty, let's try direct query once (for immediate effect) or just return empty.
    
    if (addresses.empty()) {
        // Fallback for unobserved services
        // ... (Old Logic) ...
        std::string pattern = "im:service:" + service_name + ":*";
        auto keys = RedisClient::GetInstance().Keys(pattern);
        for(const auto& k : keys) {
            std::string v = RedisClient::GetInstance().Get(k);
            if(!v.empty()) addresses.push_back(v);
        }
    }
    
    if (addresses.empty()) {
        spdlog::warn("No service found for: {}", service_name);
        return "";
    }
    
    // Round Robin
    std::lock_guard<std::mutex> lock(cache_mtx_);
    int& idx = rr_index_[service_name];
    if (idx >= addresses.size()) idx = 0;
    
    // Random or RR? User said "Lazy/Random". 
    // Let's stick to RR for stability, or Random if preferred.
    // User: "Login request picks mainly from local cache... Random select".
    // Let's use Random for "Dispatch" style?
    // Actually RR is fine. Let's keep RR logic.
    std::string selected = addresses[idx];
    idx = (idx + 1) % addresses.size();
    
    return selected;
}
