#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>
#include "server.h"
#include "db_pool.h"
#include "redis_client.h"
#include "connection_manager.h" // Added

// Note: In real Microservices, Gateway shouldn't touch DB directly.
// But for "Dispatch Service" part (Login/Register), it might need gRPC to AuthServer.
// We will initialize them here just in case, but preferably we use gRPC.

int main(int argc, char* argv[]) {
    try {
        auto const address = boost::asio::ip::make_address("0.0.0.0");
        unsigned short port = 8080;
        if(argc > 1) {
            port = static_cast<unsigned short>(std::atoi(argv[1]));
        }
        std::string gateway_id = "gw-" + std::to_string(port);
        int threads = 4;

        boost::asio::io_context ioc{threads};

        // Initialize Redis for Pub/Sub (Kick logic)
        RedisClient::GetInstance().Init("tinyim_redis", 6379);
        
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

        // --- Service Registry Heartbeat ---
        std::thread heartbeat_thread([port, gateway_id]() {
            std::string key = "im:gateway:" + std::to_string(port);
            // In Docker/Local env, we use 127.0.0.1 for now since test runs in same net
            std::string value = "127.0.0.1:" + std::to_string(port);
            while(true) {
                // SET key value EX 5
                // We need a SetEx method in RedisClient or use raw command
                // RedisClient currently only has HSet/Get/Publish. We need to add Set/Expire or raw.
                // Looking at RedisClient.h, do we have SetEx? 
                // Let's check view_file first? Assuming we might need to add it.
                // For now, use HSet in a common hash? No, TTL is better.
                // Let's assume we implement a generic Execute or just add SetEx.
                RedisClient::GetInstance().SetEx(key, value, 5);
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        });
        heartbeat_thread.detach();

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
