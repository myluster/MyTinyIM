#include "connection_manager.h"
#include "websocket_session.h"
#include <spdlog/spdlog.h>
#include "packet.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

void ConnectionManager::Join(int64_t user_id, std::shared_ptr<WebsocketSession> session) {
    std::lock_guard<std::mutex> lock(mtx_);
    users_[user_id].insert(session);
    spdlog::info("User {} joined. Total sessions: {}", user_id, users_[user_id].size());
}

void ConnectionManager::Leave(int64_t user_id, std::shared_ptr<WebsocketSession> session) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (users_.count(user_id)) {
        users_[user_id].erase(session);
        if (users_[user_id].empty()) {
            users_.erase(user_id);
        }
    }
    spdlog::info("User {} left.", user_id);
}

void ConnectionManager::SendToUser(int64_t user_id, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (users_.count(user_id)) {
        for (auto& session : users_[user_id]) {
            session->Send(msg);
        }
    }
}

void ConnectionManager::KickUser(int64_t user_id, const std::string& device) {
    // Need to identify session by device.
    // Assuming session has GetDevice()
    std::lock_guard<std::mutex> lock(mtx_);
    if (users_.count(user_id)) {
        for (auto& session : users_[user_id]) {
            if (device.empty() || session->GetDevice() == device) {
                // Send Kick Notify (CMD_LOGOUT_RESP with reason 1=Kicked)
                // Use session->Kick() for graceful close
                session->Kick();
                
                spdlog::info("Kicked user {} device {}", user_id, session->GetDevice());
            }
        }
    }
}
