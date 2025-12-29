#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>
#include "relation_service_impl.h"
#include "db_pool.h"
#include "redis_client.h"
#include "service_registry.h"

int main() {
    // Master-Slave DB Pool Init
    std::vector<std::string> slaves = {"tinyim_mysql_slave_1", "tinyim_mysql_slave_2"};
    DBPool::GetInstance().Init("tinyim_mysql_master", slaves, 3306, "root", "root", "tinyim", 10);
    
    // Init Redis for Service Registry
    RedisClient::GetInstance().Init("tinyim_redis", 6379);
    
    // Register Self to Service Registry
    int port = 50053;
    ServiceRegistry::GetInstance().Register("relation", "127.0.0.1", port);
    
    
    std::string server_address("0.0.0.0:50053"); // Port 50053 for User/Relation
    RelationServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    spdlog::info("Relation/User Server listening on {}", server_address);
    server->Wait();
    
    return 0;
}
