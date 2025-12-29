#include "gateway_service_impl.h"
#include <vector>
#include "connection_manager.h"
#include "chat.pb.h" // For MsgPushNotify
#include <spdlog/spdlog.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

Status GatewayServiceImpl::PushNotify(ServerContext* context, const tinyim::gateway::PushNotifyReq* request,
                                      tinyim::gateway::PushNotifyResp* reply) {
    int64_t user_id = request->user_id();
    int64_t max_seq = request->max_seq();
    
    // Construct MsgPushNotify Proto
    tinyim::chat::MsgPushNotify notify;
    notify.set_max_seq(max_seq);
    notify.set_type(request->msg_type()); // Need to cast or match
    
    std::string body;
    notify.SerializeToString(&body);
    
    // Construct Packet
    PacketHeader header;
    header.magic[0] = 'I'; header.magic[1] = 'M';
    header.version = 1;
    header.cmd_id = htons(CMD_MSG_PUSH_NOTIFY); // Network Byte Order
    header.body_len = htonl(body.size());
    
    std::vector<char> packet(sizeof(PacketHeader) + body.size());
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    memcpy(packet.data() + sizeof(PacketHeader), body.data(), body.size());
    
    std::string data(packet.begin(), packet.end());
    
    spdlog::info("PushNotify: user={} seq={}", user_id, max_seq);
    
    // Send to User
    ConnectionManager::GetInstance().SendToUser(user_id, data);
    
    reply->set_success(true);
    return Status::OK;
}

Status GatewayServiceImpl::KickUser(ServerContext* context, const tinyim::gateway::KickUserReq* request,
                                    tinyim::gateway::KickUserResp* reply) {
    int64_t user_id = request->user_id();
    std::string device = request->device();
    std::string reason = request->reason();
    
    spdlog::info("KickUser: user={} device={} reason={}", user_id, device, reason);
    ConnectionManager::GetInstance().KickUser(user_id, device);
    
    reply->set_success(true);
    return Status::OK;
}
