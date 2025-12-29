#pragma once

#include <grpcpp/grpcpp.h>
#include "gateway.grpc.pb.h"
#include "packet.h"

using grpc::ServerContext;
using grpc::Status;

class GatewayServiceImpl final : public tinyim::gateway::GatewayService::Service {
public:
    Status PushNotify(ServerContext* context, const tinyim::gateway::PushNotifyReq* request,
                      tinyim::gateway::PushNotifyResp* reply) override;

    Status KickUser(ServerContext* context, const tinyim::gateway::KickUserReq* request,
                    tinyim::gateway::KickUserResp* reply) override;
};
