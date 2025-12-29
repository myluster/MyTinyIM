#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "chat_service_impl.h"
#include "db_pool.h"
#include "redis_client.h"

void RunServer() {
    std::string server_address("0.0.0.0:50052");
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
    RunServer();
    return 0;
}
