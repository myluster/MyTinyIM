#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "chat_service_impl.h"
#include "db_pool.h"
#include "redis_client.h"

#include "service_registry.h" // Added

#include "config.h"

void RunServer() {
    // Init Config
    if (!Config::GetInstance().Load("config.json")) {
        Config::GetInstance().Load("../config.json");
    }

    int port = Config::GetInstance().GetInt("chat_service.port", 50052);
    std::string server_address = "0.0.0.0:" + std::to_string(port);
    
    ChatServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    spdlog::info("Chat Server listening on {}", server_address);

    // Initialize DB Pool
    std::string db_host = Config::GetInstance().GetString("mysql.host", "127.0.0.1");
    int db_port = Config::GetInstance().GetInt("mysql.port", 3306);
    std::string db_user = Config::GetInstance().GetString("mysql.user", "root");
    std::string db_pass = Config::GetInstance().GetString("mysql.password", "root");
    std::string db_name = Config::GetInstance().GetString("mysql.dbname", "tinyim");
    
    std::vector<std::string> slaves = Config::GetInstance().GetStringList("mysql.slaves");
    
    DBPool::GetInstance().Init(
        db_host, 
        slaves, 
        db_port, db_user, db_pass, db_name, 5
    );
    
    std::string redis_host = Config::GetInstance().GetString("redis.host", "127.0.0.1");
    int redis_port = Config::GetInstance().GetInt("redis.port", 6379);
    RedisClient::GetInstance().Init(redis_host, redis_port);

    server->Wait();
}

int main(int argc, char** argv) {
    if (argc > 1) {
        // Allow overriding port check if needed, but config priority?
        // Let's stick to config for simplicity or use args to override config?
        // Ignoring args for consistency with AuthServer logic
    }
    
    // Config loaded in RunServer, but we need it for Registry?
    // Let's load it here.
    if (!Config::GetInstance().Load("config.json")) {
         Config::GetInstance().Load("../config.json");
    }
    
    std::string redis_host = Config::GetInstance().GetString("redis.host", "127.0.0.1");
    int redis_port = Config::GetInstance().GetInt("redis.port", 6379);
    RedisClient::GetInstance().Init(redis_host, redis_port);
    
    // Registry
    int port = Config::GetInstance().GetInt("chat_service.port", 50052);
    ServiceRegistry::GetInstance().Register("chat_server", "127.0.0.1", port);
    
    RunServer();
    return 0;
}
