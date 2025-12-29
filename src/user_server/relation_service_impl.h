#pragma once

#include <grpcpp/grpcpp.h>
#include "relation.grpc.pb.h"
#include "chat.grpc.pb.h" // For sending system msg

using grpc::ServerContext;
using grpc::Status;

class RelationServiceImpl final : public tinyim::relation::RelationService::Service {
public:
    Status ApplyFriend(ServerContext* context, const tinyim::relation::ApplyFriendReq* request,
                       tinyim::relation::ApplyFriendResp* reply) override;

    Status AcceptFriend(ServerContext* context, const tinyim::relation::AcceptFriendReq* request,
                        tinyim::relation::AcceptFriendResp* reply) override;

    Status GetFriendList(ServerContext* context, const tinyim::relation::GetFriendListReq* request,
                         tinyim::relation::GetFriendListResp* reply) override;
                         
    Status CreateGroup(ServerContext* context, const tinyim::relation::CreateGroupReq* request,
                       tinyim::relation::CreateGroupResp* reply) override;

    Status JoinGroup(ServerContext* context, const tinyim::relation::JoinGroupReq* request,
                     tinyim::relation::JoinGroupResp* reply) override;

    Status GetGroupList(ServerContext* context, const tinyim::relation::GetGroupListReq* request,
                        tinyim::relation::GetGroupListResp* reply) override;
};
