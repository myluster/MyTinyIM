#include "test_client.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

using json = nlohmann::json;

// Helper: HTTP POST
static json HttpPost(const std::string& host, const std::string& port, const std::string& target, const json& body) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    
    auto const results = resolver.resolve(host, port);
    stream.connect(results);
    
    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::content_type, "application/json");
    req.body() = body.dump();
    req.prepare_payload();
    
    http::write(stream, req);
    
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    
    stream.socket().shutdown(tcp::socket::shutdown_both);
    return json::parse(res.body());
}

TestClient::TestClient(const std::string& host, const std::string& http_port, const std::string& ws_port)
    : host_(host), http_port_(http_port), ws_port_(ws_port) {
}

TestClient::~TestClient() {
    Close();
}

std::string TestClient::Login(const std::string& username, const std::string& password, const std::string& device) {
    // 1. Register (with retry)
    bool registered = false;
    for (int i = 0; i < 3; i++) {
        try {
            json reg = {{"username", username}, {"password", password}, {"nickname", username}};
            json resp = HttpPost(host_, http_port_, "/api/register", reg);
            if (resp["code"] == 0 || resp["msg"].get<std::string>().find("exists") != std::string::npos) {
                registered = true;
                break;
            }
        } catch(...) {
            // Retry
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Small delay to ensure registration is committed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 2. Login
    json login = {{"username", username}, {"password", password}, {"device", device}};
    json resp = HttpPost(host_, http_port_, "/api/login", login);
    
    if (resp["code"] != 0) throw std::runtime_error("Login Failed: " + resp.value("msg", "unknown"));
    
    user_id_ = resp["data"]["user_id"];
    if (resp["data"].contains("gateway_url")) {
        gateway_url_ = resp["data"]["gateway_url"];
    }
    return resp["data"]["token"];
}

void TestClient::Connect(int64_t uid, const std::string& token, const std::string& device, const std::string& url_override, bool enable_heartbeat) {
    // Clean up any existing connection first
    if (running_ || ioc_thread_.joinable()) {
        Close();
    }
    
    enable_heartbeat_ = enable_heartbeat;
    user_id_ = uid;
    ioc_ = std::make_shared<net::io_context>();
    ws_ = std::make_shared<websocket::stream<beast::tcp_stream>>(*ioc_);
    
    // Parse URL if overridden, else use default host/ws_port
    std::string target_host = host_;
    std::string target_port = ws_port_;
    std::string target_path = "/ws";

    if (!url_override.empty()) {
        // ws://127.0.0.1:8080/ws
        // Simple parse
        std::string p = url_override;
        if (p.find("ws://") == 0) p = p.substr(5);
        auto colon = p.find(':');
        auto slash = p.find('/');
        if (colon != std::string::npos) {
            target_host = p.substr(0, colon);
            if (slash != std::string::npos) {
                target_port = p.substr(colon + 1, slash - (colon + 1));
                target_path = p.substr(slash);
            } else {
                target_port = p.substr(colon + 1);
            }
        }
    } else if (!gateway_url_.empty()) {
        // Use the one from login if available and not overridden
        // But usually tests might want to force localhost for simplified test setup
        // Let's assume we use default unless overridden, OR use gateway_url if valid.
        // For LB test we will pass gateway_url explicitly.
    }

    tcp::resolver resolver(*ioc_);
    auto results = resolver.resolve(target_host, target_port);
    net::connect(ws_->next_layer().socket(), results);
    
    // Handshake
    std::string path = target_path + "?id=" + std::to_string(uid) + "&token=" + token + "&device=" + device;
    ws_->handshake(target_host, path);
    ws_->binary(true);
    
    running_ = true;
    
    // Start Read Thread
    ioc_thread_ = std::thread([this] {
        try {
            ReadLoop();
        } catch(...) {
            // Closed
        }
    });
    
    // Start Heartbeat Thread (if enabled)
    if (enable_heartbeat_) {
        heartbeat_thread_ = std::thread([this] {
            HeartbeatLoop();
        });
    }
}

void TestClient::HeartbeatLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (running_) {
            try {
                SendHeartbeat();
            } catch(...) {
                // Ignore heartbeat errors
            }
        }
    }
}

void TestClient::SendHeartbeat() {
    PacketHeader header;
    header.cmd_id = htons(CMD_HEARTBEAT_REQ);
    header.body_len = htonl(0);
    
    std::vector<char> packet(sizeof(header));
    memcpy(packet.data(), &header, sizeof(header));
    
    if (ws_ && ws_->is_open()) {
        beast::error_code ec;
        ws_->write(net::buffer(packet), ec);
        // Ignore errors
    }
}

void TestClient::SendPacket(uint16_t cmd_id, const google::protobuf::Message& msg) {
    std::string body;
    msg.SerializeToString(&body);
    
    PacketHeader header;
    header.cmd_id = htons(cmd_id);
    header.body_len = htonl(body.size());
    
    std::string packet;
    packet.resize(sizeof(header) + body.size());
    memcpy(&packet[0], &header, sizeof(header));
    memcpy(&packet[sizeof(header)], body.data(), body.size());
    
    ws_->write(net::buffer(packet));
}


void TestClient::ReadLoop() {
    beast::flat_buffer buffer;
    while(running_) {
        beast::error_code ec;
        ws_->read(buffer, ec);
        
        // Handle connection closure or errors
        if (ec == websocket::error::closed || ec == beast::error::timeout || ec) {
            spdlog::info("TestClient: Connection closed/error: {}", ec.message());
            running_ = false;
            break;
        }
        
        while (buffer.size() >= sizeof(PacketHeader)) {
            const char* ptr = static_cast<const char*>(buffer.data().data());
            PacketHeader header;
            memcpy(&header, ptr, sizeof(header));
            
            uint32_t len = ntohl(header.body_len);
            if (buffer.size() < sizeof(header) + len) break;
            
            RecvPacket pkt;
            pkt.cmd_id = ntohs(header.cmd_id);
            pkt.body = std::string(ptr + sizeof(header), len);
            
            {
                std::lock_guard<std::mutex> lk(mtx_);
                inbox_.push(pkt);
            }
            cv_.notify_one();
            
            buffer.consume(sizeof(header) + len);
        }
    }
}

bool TestClient::WaitForPacket(uint16_t expected_cmd, std::string& out_body, int timeout_ms) {
    std::unique_lock<std::mutex> lk(mtx_);
    auto start = std::chrono::steady_clock::now();
    
    while(true) {
        if (!inbox_.empty()) {
            RecvPacket pkt = inbox_.front();
            inbox_.pop();
            
            // Log for debug
            spdlog::info("Client[{}] Recv Cmd=0x{:x}", user_id_, pkt.cmd_id);
            
            if (pkt.cmd_id == expected_cmd) {
                out_body = pkt.body;
                return true;
            }
            // Ignore other packets (e.g. HeartbeatResp)
        }
        
        if (cv_.wait_for(lk, std::chrono::milliseconds(100)) == std::cv_status::timeout) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeout_ms) {
                return false;
            }
        }
    }
}

void TestClient::Close() {
    // Step 1: Signal ReadLoop and HeartbeatLoop to stop
    running_ = false;
    
    // Step 2: Shutdown the underlying socket to interrupt any blocking read
    try {
        if (ws_) {
            beast::error_code ec;
            // Shutdown the TCP socket to interrupt blocking reads
            ws_->next_layer().socket().shutdown(tcp::socket::shutdown_both, ec);
            // Ignore errors - socket might already be closed
        }
    } catch(...) {
        // Ignore exceptions
    }
    
    // Step 3: Now ReadLoop and HeartbeatLoop should exit quickly, wait for them
    if (ioc_thread_.joinable()) {
        ioc_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    // Step 4: Close WebSocket gracefully (if still open)
    try {
        if (ws_ && ws_->is_open()) {
            beast::error_code ec;
            ws_->close(websocket::close_code::normal, ec);
        }
    } catch(...) {
        // Ignore all exceptions during cleanup
    }
    
    // Step 5: Clean up resources
    ws_.reset();
    ioc_.reset();
}
