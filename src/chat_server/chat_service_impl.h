#pragma once

#include "chat.grpc.pb.h"
#include "db_pool.h"
#include "redis_client.h"

using grpc::ServerContext;
using grpc::Status;

class ChatServiceImpl final : public tinyim::chat::ChatService::Service {
public:
    Status SendMessage(ServerContext* context, const tinyim::chat::SendMessageReq* request,
                       tinyim::chat::SendMessageResp* reply) override;

    Status SyncMessages(ServerContext* context, const tinyim::chat::SyncMessagesReq* request,
                        tinyim::chat::SyncMessagesResp* reply) override;
};
