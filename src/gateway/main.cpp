#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include "server.h"
#include "db_pool.h"
#include "redis_client.h"
#include "config.h"
#include "connection_manager.h" // Added
#include "gateway_service_impl.h" // Added
#include "service_registry.h" // Added

// Note: In real Microservices, Gateway shouldn't touch DB directly.
// But for "Dispatch Service" part (Login/Register), it might need gRPC to AuthServer.
// We will initialize them here just in case, but preferably we use gRPC.

int main(int argc, char* argv[]) {
    try {
        if (!Config::GetInstance().Load("config.json")) {
            Config::GetInstance().Load("../config.json");
        }
        
        auto const address = boost::asio::ip::make_address("0.0.0.0");
        unsigned short port = static_cast<unsigned short>(Config::GetInstance().GetInt("server.port", 8080));
        
        if(argc > 1) {
            port = static_cast<unsigned short>(std::atoi(argv[1]));
        }
        
        std::string gateway_id = "gw-" + std::to_string(port);
        int threads = 4;

        boost::asio::io_context ioc{threads};

        // Initialize Redis for Pub/Sub (Kick logic)
        std::string redis_host = Config::GetInstance().GetString("redis.host", "127.0.0.1");
        int redis_port = Config::GetInstance().GetInt("redis.port", 6379);
        RedisClient::GetInstance().Init(redis_host, redis_port);
        
        // Start Kick Listener Thread
        std::thread subscribe_thread([gateway_id]() {
            RedisClient::GetInstance().Subscribe("im:kick", [gateway_id](const std::string& msg) {
                // ... (Kick logic remains)
                // Msg format: user_id:device
                auto pos = msg.find(':');
                if (pos != std::string::npos) {
                    int64_t uid = std::stoll(msg.substr(0, pos));
                    std::string dev = msg.substr(pos + 1);
                    spdlog::info("[{}] Received Kick Event for user={} dev={}", gateway_id, uid, dev);
                    ConnectionManager::GetInstance().KickUser(uid, dev);
                }
            });
        });
        subscribe_thread.detach(); 

        // Create gRPC Server
        std::string grpc_address = "0.0.0.0:" + std::to_string(port + 10000); // 8080 -> 18080
        GatewayServiceImpl gateway_service;
        
        grpc::ServerBuilder builder;
        builder.AddListeningPort(grpc_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&gateway_service);
        std::unique_ptr<grpc::Server> grpc_server(builder.BuildAndStart());
        
        spdlog::info("[{}] Gateway RPC Server running on {}", gateway_id, grpc_address);
        
        // --- Service Registry ---
        // Register myself
        // Use 127.0.0.1 for local discovery
        ServiceRegistry::GetInstance().Register("gateway", "127.0.0.1", port);
        
        // Start Observing Gateways (for Load Balancing)
        ServiceRegistry::GetInstance().Observe("gateway");

        // Start the server
        std::make_shared<Server>(ioc, tcp::endpoint{address, port})->Run();

        spdlog::info("[{}] Gateway Server running on port {}", gateway_id, port);

        // Run io_context on multiple threads
        std::vector<std::thread> v;
        v.reserve(threads - 1);
        for(auto i = threads - 1; i > 0; --i)
            v.emplace_back([&ioc]{ ioc.run(); });
        
        ioc.run();
    } catch (const std::exception& e) {
        spdlog::error("Fatal Error: {}", e.what());
        return -1;
    }
    return 0;
}
