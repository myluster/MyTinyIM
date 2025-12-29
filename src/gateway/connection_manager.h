#pragma once
#include <memory>
#include <mutex>
#include <unordered_map>
#include <set>
#include <string>

class WebsocketSession;

class ConnectionManager {
public:
    static ConnectionManager& GetInstance() {
        static ConnectionManager instance;
        return instance;
    }

    void Join(int64_t user_id, std::shared_ptr<WebsocketSession> session);
    void Leave(int64_t user_id, std::shared_ptr<WebsocketSession> session);
    
    // Send to specific user (all devices)
    void SendToUser(int64_t user_id, const std::string& msg);
    
    // Kick specific device or all
    void KickUser(int64_t user_id, const std::string& device = "");

private:
    std::mutex mtx_;
    // map: user_id -> set of sessions
    std::unordered_map<int64_t, std::set<std::shared_ptr<WebsocketSession>>> users_;
};
