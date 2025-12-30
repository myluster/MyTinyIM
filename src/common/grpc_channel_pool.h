#pragma once

#include <grpcpp/grpcpp.h>
#include <string>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <spdlog/spdlog.h>

class GRPCChannelPool {
public:
    static GRPCChannelPool& GetInstance() {
        static GRPCChannelPool instance;
        return instance;
    }

    std::shared_ptr<grpc::Channel> GetChannel(const std::string& address) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = channels_.find(address);
        if (it != channels_.end()) {
            // Check state? 
            // grpc::Channel manages connection state internally (Idle, Connecting, Ready...)
            // If it's Shutdown, we might need to recreate. But usually Client Channels persist.
            return it->second;
        }

        // Create new
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        channels_[address] = channel;
        spdlog::info("Created new gRPC channel to {}", address);
        return channel;
    }

private:
    GRPCChannelPool() = default;
    std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<grpc::Channel>> channels_;
};
