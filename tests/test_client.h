#pragma once
#include <string>
#include <thread>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include "packet.h"
#include <google/protobuf/message.h>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

struct RecvPacket {
    uint16_t cmd_id;
    std::string body;
};

class TestClient {
public:
    TestClient(const std::string& host, const std::string& http_port, const std::string& ws_port);
    ~TestClient();

    // 1. HTTP Register & Login -> Return Token
    std::string Login(const std::string& username, const std::string& password, const std::string& device = "PC");
    
    // Returns the gateway url received from login (e.g. ws://127.0.0.1:8080/ws)
    std::string GetGatewayUrl() const { return gateway_url_; }

    // 2. WebSocket Connect
    // If url override is empty, use constructor default.
    void Connect(int64_t uid, const std::string& token, const std::string& device, const std::string& url_override = "", bool enable_heartbeat = true);
    
    // 3. Send
    void SendPacket(uint16_t cmd_id, const google::protobuf::Message& msg);
    
    // 4. Wait for Packet
    // If exact_cmd provided, waits for that specific cmd.
    // Else waits for any packet.
    bool WaitForPacket(uint16_t expected_cmd, std::string& out_body, int timeout_ms = 2000);
    
    int64_t GetUserId() const { return user_id_; }
    bool IsRunning() const { return running_; }
    void Close();
    
    // Force stop read loop (for heartbeat timeout test)
    void Shutdown() { 
        running_ = false; 
        if (ws_) {
             boost::system::error_code ec;
             ws_->next_layer().socket().shutdown(tcp::socket::shutdown_both, ec);
             ws_->close(websocket::close_code::normal, ec); 
        }
    }

private:
    void ReadLoop();
    void HeartbeatLoop();
    void SendHeartbeat();
    
    std::string host_;
    std::string http_port_;
    std::string ws_port_;
    std::string gateway_url_; // Store from Login
    
    int64_t user_id_ = 0;
    
    // Net
    std::shared_ptr<net::io_context> ioc_;
    std::thread ioc_thread_;
    std::thread heartbeat_thread_;
    std::shared_ptr<websocket::stream<beast::tcp_stream>> ws_;
    
    // Queue
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<RecvPacket> inbox_;
    std::atomic<bool> running_{false};
    bool enable_heartbeat_ = true;
};
