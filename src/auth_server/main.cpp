#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "auth_service_impl.h"
#include "db_pool.h"
#include "redis_client.h"
#include "config.h"

void RunServer() {
    // Init Config
    if (!Config::GetInstance().Load("config.json")) {
        Config::GetInstance().Load("../config.json");
    }

    int port = Config::GetInstance().GetInt("auth_service.port", 50051);
    std::string server_address("0.0.0.0:" + std::to_string(port));
    AuthServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    spdlog::info("Auth Server listening on {}", server_address);

    // Initialize DB & Redis
    std::string db_host = Config::GetInstance().GetString("mysql.host", "127.0.0.1");
    int db_port = Config::GetInstance().GetInt("mysql.port", 3306);
    std::string db_user = Config::GetInstance().GetString("mysql.user", "root");
    std::string db_pass = Config::GetInstance().GetString("mysql.password", "root");
    std::string db_name = Config::GetInstance().GetString("mysql.dbname", "tinyim");
    
    DBPool::GetInstance().Init(
        db_host, 
        {}, // Slaves
        db_port, db_user, db_pass, db_name, 5
    );
    
    std::string redis_host = Config::GetInstance().GetString("redis.host", "127.0.0.1");
    int redis_port = Config::GetInstance().GetInt("redis.port", 6379);
    RedisClient::GetInstance().Init(redis_host, redis_port);

    server->Wait();
}

int main(int argc, char** argv) {
    RunServer();
    return 0;
}
