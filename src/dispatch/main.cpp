#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include "service_discovery.h"
#include "config.h"
#include "auth.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include "grpc_channel_pool.h"
#include <fstream>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using namespace tinyim::auth;

// gRPC Client Helper
std::string Auth(const std::string& username, const std::string& password, const std::string& device, int64_t& uid, std::string& nick) {
    // Determine Auth Service Address (Config or Service Discovery?)
    // For MVP config.json
    // Use GRPCChannelPool!
    std::string auth_addr = Config::GetInstance().GetString("auth_service.addr", "127.0.0.1:50051");
    auto start = std::chrono::high_resolution_clock::now();
    
    auto channel = GRPCChannelPool::GetInstance().GetChannel(auth_addr); // Uses Pool
    auto stub = AuthService::NewStub(channel);
    
    LoginReq req;
    req.set_username(username);
    req.set_password(password);
    req.set_device(device);
    
    LoginResp resp;
    grpc::ClientContext context;
    grpc::Status status = stub->Login(&context, req, &resp);
    
    if (status.ok() && resp.success()) {
        uid = resp.user_id();
        nick = resp.nickname();
        return resp.token();
    }
    return "";
}

// Logout Helper
bool Logout(int64_t uid, const std::string& device) {
    std::string auth_addr = Config::GetInstance().GetString("auth_service.addr", "127.0.0.1:50051");
    auto channel = GRPCChannelPool::GetInstance().GetChannel(auth_addr);
    auto stub = AuthService::NewStub(channel);
    
    LogoutReq req;
    req.set_user_id(uid);
    req.set_device(device);
    
    LogoutResp resp;
    grpc::ClientContext context;
    grpc::Status status = stub->Logout(&context, req, &resp);
    
    return (status.ok() && resp.success());
}

// Register Helper
bool RegisterUser(const std::string& username, const std::string& password, const std::string& nickname) {
    std::string auth_addr = Config::GetInstance().GetString("auth_service.addr", "127.0.0.1:50051");
    auto channel = GRPCChannelPool::GetInstance().GetChannel(auth_addr);
    auto stub = AuthService::NewStub(channel);
    
    RegisterReq req;
    req.set_username(username);
    req.set_password(password);
    req.set_nickname(nickname);
    
    RegisterResp resp;
    grpc::ClientContext context;
    grpc::Status status = stub->Register(&context, req, &resp);
    
    return (status.ok() && resp.success());
}

// HTTP Session
class HttpSession : public std::enable_shared_from_this<HttpSession> {
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    http::response<http::string_body> res_; // Member to keep alive during async_write

public:
    HttpSession(tcp::socket socket) : socket_(std::move(socket)) {}

    void Run() {
        DoRead();
    }

private:
    void DoRead() {
        // Clear request for new read
        req_ = {};
        
        auto self = shared_from_this();
        http::async_read(socket_, buffer_, req_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                if(ec == http::error::end_of_stream) {
                    self->socket_.shutdown(tcp::socket::shutdown_send, ec);
                    return;
                }
                if(ec) return; // Error
                self->ProcessRequest();
            });
    }

    void ProcessRequest() {
        res_ = {}; // Clear previous response
        res_.version(req_.version());
        res_.keep_alive(req_.keep_alive());
        
        res_.set(http::field::server, "TinyIM Dispatch");
        res_.set(http::field::content_type, "application/json");
        res_.set(http::field::access_control_allow_origin, "*");
        res_.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res_.set(http::field::access_control_allow_headers, "Content-Type");

        if (req_.method() == http::verb::options) {
            res_.result(http::status::ok);
            res_.body() = "";
            res_.prepare_payload();
            WriteResponse();
            return;
        }

        std::string target = std::string(req_.target());
        
        if (req_.method() == http::verb::post && target == "/api/login") {
            try {
                auto json = nlohmann::json::parse(req_.body());
                std::string u = json.value("username", "");
                std::string p = json.value("password", "");
                std::string d = json.value("device", "PC");
                
                int64_t uid = 0;
                std::string nick;
                std::string token = Auth(u, p, d, uid, nick);
                
                if (!token.empty()) {
                    std::string gw = ServiceDiscovery::GetInstance().GetRandomGateway();
                    if (gw.empty()) {
                        res_.result(http::status::service_unavailable);
                        res_.body() = R"({"error": "No gateways available"})";
                    } else {
                        nlohmann::json resp_json;
                        resp_json["code"] = 0;
                        resp_json["data"] = {
                            {"user_id", uid},
                            {"token", token},
                            {"nickname", nick},
                            {"gateway_url", "ws://" + gw + "/ws"}
                        };
                        res_.body() = resp_json.dump();
                    }
                } else {
                    res_.result(http::status::unauthorized);
                    res_.body() = R"({"error": "Invalid credentials"})";
                }
            } catch(...) {
                res_.result(http::status::bad_request);
                res_.body() = "Invalid JSON";
            }
        }
        else if (req_.method() == http::verb::post && target == "/api/logout") {
             try {
                auto json = nlohmann::json::parse(req_.body());
                int64_t uid = json.value("user_id", 0);
                std::string dev = json.value("device", "");
                if (uid > 0) Logout(uid, dev);
                res_.body() = R"({"code": 0, "msg": "Logged out"})";
             } catch(...) {
                 res_.result(http::status::bad_request);
                 res_.body() = "Invalid JSON";
             }
        }
        else if (req_.method() == http::verb::post && target == "/api/register") {
             try {
                auto json = nlohmann::json::parse(req_.body());
                if (RegisterUser(json.value("username",""), json.value("password",""), json.value("nickname",""))) {
                     res_.body() = R"({"code": 0, "msg": "Registered"})";
                } else {
                     res_.body() = R"({"code": 1, "msg": "Register Failed"})";
                }
             } catch(...) {
                 res_.result(http::status::bad_request);
                 res_.body() = "Invalid JSON";
             }
        }
        else if (req_.method() == http::verb::get && target == "/api/discover/chat") {
             try {
                 std::string gw = ServiceDiscovery::GetInstance().GetRandomGateway();
                 if (gw.empty()) {
                      res_.result(http::status::service_unavailable);
                      res_.body() = R"({"error": "No gateways available"})";
                 } else {
                      nlohmann::json resp_json;
                      resp_json["code"] = 0;
                      resp_json["data"] = {
                          {"gateway_url", "ws://" + gw + "/ws"}
                      };
                      res_.body() = resp_json.dump();
                 }
             } catch (const std::exception& e) {
                 spdlog::error("Error in /api/discover/chat: {}", e.what());
                 // File Log
                 std::ofstream err_file("/app/logs/dispatch_err.txt", std::ios::app);
                 if (err_file) {
                     err_file << "CRITICAL ERROR: " << e.what() << std::endl;
                 }
                 res_.result(http::status::internal_server_error);
                 res_.body() = std::string("{\"error\": \"") + e.what() + "\"}";
             } catch (...) {
                 spdlog::error("Unknown error in /api/discover/chat");
                 // File Log
                 std::ofstream err_file("/app/logs/dispatch_err.txt", std::ios::app);
                 if (err_file) {
                     err_file << "CRITICAL ERROR: Unknown Exception" << std::endl;
                 }
                 res_.result(http::status::internal_server_error);
                 res_.body() = "{\"error\": \"Unknown\"}";
             }
        }
        else {
            res_.result(http::status::not_found);
            res_.body() = "Not Found";
        }
        
        res_.prepare_payload();
        WriteResponse();
    }
    
    void WriteResponse() {
        auto self = shared_from_this();
        http::async_write(socket_, res_, [self](beast::error_code ec, std::size_t) {
            self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            // In a full implementation, we should check res_.need_eof() to decide whether to shutdown or Loop DoRead().
            // However, for simplicity and to avoid lingering sockets in this MVP, 
            // we force close (short-lived connections).
            // But Vite Proxy sends Keep-Alive and might be upset if we close abruptly without sending Connection: close header?
            // Let's rely on shutdown sending FIN.
            // If we want Keep-Alive:
            // if(!ec && self->res_.keep_alive()) {
            //    self->DoRead(); 
            // } else {
            //    self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            // }
            // Given the issues, let's try Proper Keep-Alive now:
            if(!ec && self->res_.keep_alive()) {
                self->DoRead();
            } else {
                 self->socket_.shutdown(tcp::socket::shutdown_send, ec);
            }
        });
    }
};

// Listener
void DoAccept(tcp::acceptor& acceptor, net::io_context& ioc) {
    acceptor.async_accept(
        net::make_strand(ioc),
        [&acceptor, &ioc](beast::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<HttpSession>(std::move(socket))->Run();
            }
            DoAccept(acceptor, ioc);
        });
}

int main(int argc, char* argv[]) {
    // Init Config
    if (!Config::GetInstance().Load("config.json")) {
        Config::GetInstance().Load("../config.json"); // Try parent
    }
    
    // Init Redis for ServiceDiscovery
    std::string redis_host = Config::GetInstance().GetString("redis.host", "127.0.0.1");
    int redis_port = Config::GetInstance().GetInt("redis.port", 6379);
    RedisClient::GetInstance().Init(redis_host, redis_port);
    
    // Start Service Discovery
    ServiceDiscovery::GetInstance().Start();
    
    int port = Config::GetInstance().GetInt("server.dispatch_port", 8000);
    net::io_context ioc{1};
    tcp::acceptor acceptor{ioc, {tcp::v4(), (unsigned short)port}};
    
    spdlog::info("Dispatch Server listening on port {}", port);
    
    DoAccept(acceptor, ioc);
    
    ioc.run();
    
    return 0;
}
