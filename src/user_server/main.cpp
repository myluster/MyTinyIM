#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>
#include "relation_service_impl.h"
#include "db_pool.h"
#include "redis_client.h"
#include "service_registry.h"

#include "config.h"

int main(int argc, char** argv) {
    if (argc > 1) {
    }
    
    // Init Config
    if (!Config::GetInstance().Load("config.json")) {
        Config::GetInstance().Load("../config.json");
    }

    // Master-Slave DB Pool Init
    std::string db_host = Config::GetInstance().GetString("mysql.host", "127.0.0.1");
    int db_port = Config::GetInstance().GetInt("mysql.port", 3306);
    std::string db_user = Config::GetInstance().GetString("mysql.user", "root");
    std::string db_pass = Config::GetInstance().GetString("mysql.password", "root");
    std::string db_name = Config::GetInstance().GetString("mysql.dbname", "tinyim");
    
    std::vector<std::string> slaves = Config::GetInstance().GetStringList("mysql.slaves");
    
    DBPool::GetInstance().Init(db_host, slaves, db_port, db_user, db_pass, db_name, 10);
    
    // Init Redis for Service Registry
    std::string redis_host = Config::GetInstance().GetString("redis.host", "127.0.0.1");
    int redis_port = Config::GetInstance().GetInt("redis.port", 6379);
    RedisClient::GetInstance().Init(redis_host, redis_port);
    
    // Register Self to Service Registry
    int port = Config::GetInstance().GetInt("user_service.port", 50053);
    // Use localhost IP logic or config IP? Ideally config.
    ServiceRegistry::GetInstance().Register("relation", "127.0.0.1", port);
    
    
    std::string server_address("0.0.0.0:" + std::to_string(port)); 
    RelationServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    spdlog::info("Relation/User Server listening on {}", server_address);
    server->Wait();
    
    return 0;
}
