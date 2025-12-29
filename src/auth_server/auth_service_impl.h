#pragma once

#include "auth.grpc.pb.h"
#include "db_pool.h"
#include "redis_client.h"

using grpc::ServerContext;
using grpc::Status;

class AuthServiceImpl final : public tinyim::auth::AuthService::Service {
public:
    Status Register(ServerContext* context, const tinyim::auth::RegisterReq* request,
                    tinyim::auth::RegisterResp* reply) override;

    Status Login(ServerContext* context, const tinyim::auth::LoginReq* request,
                 tinyim::auth::LoginResp* reply) override;

    Status Logout(ServerContext* context, const tinyim::auth::LogoutReq* request,
                  tinyim::auth::LogoutResp* reply) override;
};
