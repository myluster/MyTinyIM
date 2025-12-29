#include "http_session.h"
#include "websocket_session.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "redis_client.h"
#include <grpcpp/grpcpp.h>
#include "auth.grpc.pb.h"

using json = nlohmann::json;

// gRPC Client Helper
class AuthClient {
public:
    AuthClient() {
        // Connect to local Auth Server
        channel_ = grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
        stub_ = tinyim::auth::AuthService::NewStub(channel_);
    }

    std::unique_ptr<tinyim::auth::AuthService::Stub> stub_;
    std::shared_ptr<grpc::Channel> channel_;
};

// Singleton accessor
static AuthClient& GetAuthClient() {
    static AuthClient client;
    return client;
}

HttpSession::HttpSession(tcp::socket&& socket)
    : stream_(std::move(socket)) {}

void HttpSession::Run() {
    DoRead();
}

void HttpSession::DoRead() {
    req_ = {};
    stream_.expires_after(std::chrono::seconds(30));
    http::async_read(stream_, buffer_, req_,
        beast::bind_front_handler(&HttpSession::OnRead, shared_from_this()));
}

void HttpSession::OnRead(beast::error_code ec, std::size_t bytes_transferred) {
    if(ec == http::error::end_of_stream) {
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }
    if(ec) return (void)spdlog::error("Http Read failed: {}", ec.message());

    // Check for WebSocket Upgrade
    if (websocket::is_upgrade(req_)) {
        // Parse Query: /ws?token=x&id=y&device=z
        std::string target(req_.target());
        auto q_pos = target.find('?');
        int64_t user_id = 0;
        std::string token, device = "PC";
        
        if (q_pos != std::string::npos) {
             std::string query = target.substr(q_pos + 1);
             std::stringstream ss(query);
             std::string segment;
             while(std::getline(ss, segment, '&')) {
                 auto eq_pos = segment.find('=');
                 if(eq_pos != std::string::npos) {
                     std::string key = segment.substr(0, eq_pos);
                     std::string val = segment.substr(eq_pos + 1);
                     if(key == "id") user_id = std::stoll(val);
                     else if(key == "token") token = val;
                     else if(key == "device") device = val;
                 }
             }
        }
        
        // Auth Check
        bool auth_ok = false;
        if (user_id > 0 && !token.empty()) {
             std::string session_key = "im:session:" + std::to_string(user_id);
             std::string stored_token = RedisClient::GetInstance().HGet(session_key, device);
             if (stored_token == token) {
                 auth_ok = true;
             } else {
                 spdlog::warn("WS Auth Failed for user {}: Check token/device mismatch.", user_id);
             }
        }
        
        if (!auth_ok) {
             http::response<http::string_body> res{http::status::unauthorized, req_.version()};
             res.set(http::field::server, "TinyIM Gateway");
             res.body() = "Auth Failed";
             res.prepare_payload();
             return DoWrite(std::move(res));
        }

        auto ws = std::make_shared<WebsocketSession>(stream_.release_socket());
        ws->SetUserInfo(user_id, device);
        ws->DoAccept(std::move(req_));
        return;
    }

    HandleRequest();
}

void HttpSession::HandleRequest() {
    http::response<http::string_body> res{http::status::ok, req_.version()};
    res.set(http::field::server, "TinyIM Gateway");
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req_.keep_alive());

    if (req_.method() != http::verb::post) {
        res.result(http::status::bad_request);
        res.body() = "Only POST allowed";
        return DoWrite(std::move(res));
    }

    std::string target = std::string(req_.target());
    
    // --- Register ---
    if (target == "/api/register") {
        try {
            auto body = json::parse(req_.body());
            tinyim::auth::RegisterReq rpc_req;
            rpc_req.set_username(body.value("username", ""));
            rpc_req.set_password(body.value("password", ""));
            rpc_req.set_nickname(body.value("nickname", ""));

            tinyim::auth::RegisterResp rpc_resp;
            grpc::ClientContext context;
            
            grpc::Status status = GetAuthClient().stub_->Register(&context, rpc_req, &rpc_resp);
            
            json resp_json;
            if (status.ok() && rpc_resp.success()) {
                resp_json["code"] = 0;
                resp_json["msg"] = "Register Success";
                resp_json["data"] = {{"user_id", rpc_resp.user_id()}};
            } else {
                resp_json["code"] = 1;
                resp_json["msg"] = !status.ok() ? ("RPC Error: " + status.error_message()) : rpc_resp.error_message();
            }
            res.body() = resp_json.dump();
        } catch (...) {
            res.result(http::status::bad_request);
            res.body() = "Invalid JSON";
        }
    } 
    // --- Login ---
    else if (target == "/api/login") {
        try {
            auto body = json::parse(req_.body());
            tinyim::auth::LoginReq rpc_req;
            rpc_req.set_username(body.value("username", ""));
            rpc_req.set_password(body.value("password", ""));
            rpc_req.set_device(body.value("device", "PC"));

            tinyim::auth::LoginResp rpc_resp;
            grpc::ClientContext context;

            grpc::Status status = GetAuthClient().stub_->Login(&context, rpc_req, &rpc_resp);

            json resp_json;
            if (status.ok() && rpc_resp.success()) {
                // LB Logic
                std::string gateway_url = "ws://127.0.0.1:8080/ws"; // fallback
                try {
                    auto keys = RedisClient::GetInstance().Keys("im:gateway:*");
                    if (!keys.empty()) {
                        static std::random_device rd;
                        static std::mt19937 gen(rd());
                        std::uniform_int_distribution<> dis(0, keys.size() - 1);
                        std::string addr = RedisClient::GetInstance().Get(keys[dis(gen)]);
                        if(!addr.empty()) gateway_url = "ws://" + addr + "/ws";
                    }
                } catch(...) {
                    spdlog::error("LB Selection failed");
                }

                resp_json["code"] = 0;
                resp_json["msg"] = "Login Success";
                resp_json["data"] = {
                    {"user_id", rpc_resp.user_id()},
                    {"token", rpc_resp.token()},
                    {"gateway_url", gateway_url} 
                };
            } else {
                resp_json["code"] = 1;
                resp_json["msg"] = !status.ok() ? ("RPC Error: " + status.error_message()) : rpc_resp.error_message();
            }
            res.body() = resp_json.dump();
        } catch (...) {
            res.result(http::status::bad_request);
            res.body() = "Invalid JSON";
        }
    } 
    // --- Logout ---
    else if (target == "/api/logout") {
        try {
            auto body = json::parse(req_.body());
            tinyim::auth::LogoutReq rpc_req;
            rpc_req.set_user_id(body.value("user_id", 0));
            rpc_req.set_token(body.value("token", ""));
            rpc_req.set_device(body.value("device", "PC"));

            tinyim::auth::LogoutResp rpc_resp;
            grpc::ClientContext context;
            grpc::Status status = GetAuthClient().stub_->Logout(&context, rpc_req, &rpc_resp);

            json resp_json;
            if (status.ok() && rpc_resp.success()) {
                resp_json["code"] = 0;
                resp_json["msg"] = "Logout Success";
            } else {
                resp_json["code"] = 1;
                resp_json["msg"] = !status.ok() ? ("RPC Error: " + status.error_message()) : "Logout Failed";
            }
            res.body() = resp_json.dump();
        } catch (...) {
            res.result(http::status::bad_request);
             res.body() = "Invalid JSON";
        }
    } 
    // --- Not Found ---
    else {
        res.result(http::status::not_found);
        res.body() = "Not Found";
    }

    res.prepare_payload();
    DoWrite(std::move(res));
}

void HttpSession::DoWrite(http::response<http::string_body>&& res) {
    auto sp = std::make_shared<http::response<http::string_body>>(std::move(res));
    http::async_write(stream_, *sp,
        [self = shared_from_this(), sp](beast::error_code ec, std::size_t bytes) {
            self->stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        });
}
