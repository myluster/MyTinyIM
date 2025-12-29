#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "chat_service_impl.h"
#include "db_pool.h"
#include "redis_client.h"

#include "service_registry.h" // Added

void RunServer(int port) {
    std::string server_address = "0.0.0.0:" + std::to_string(port);
    ChatServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    spdlog::info("Chat Server listening on {}", server_address);

    // Initialize DB Pool (Read/Write Splitting)
    DBPool::GetInstance().Init(
        "tinyim_mysql_master", 
        {"tinyim_mysql_slave_1", "tinyim_mysql_slave_2"},
        3306, "root", "root", "tinyim", 5
    );
    RedisClient::GetInstance().Init("tinyim_redis", 6379);

    server->Wait();
}

int main(int argc, char** argv) {
    // Parse port from args or default
    int port = 50052;
    if (argc > 1) port = std::atoi(argv[1]);
    
    // Init Service Registry
    // We need to Init Redis first
    RedisClient::GetInstance().Init("tinyim_redis", 6379);
    
    // Register Self
    // Use actual IP in production, here assume local or container IP
    ServiceRegistry::GetInstance().Register("chat", "127.0.0.1", port);
    
    RunServer(port);
    return 0;
}
