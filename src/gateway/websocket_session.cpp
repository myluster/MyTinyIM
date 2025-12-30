#include "websocket_session.h"
#include "connection_manager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include "packet.h"
#include "chat.grpc.pb.h"
#include "relation.grpc.pb.h"
#include "redis_client.h" // Added header
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif
namespace http = boost::beast::http;
using grpc::Status; // Added using

WebsocketSession::WebsocketSession(tcp::socket&& socket)
    : ws_(std::move(socket))
    , strand_(boost::asio::make_strand(static_cast<boost::asio::io_context&>(ws_.get_executor().context()))) {
    
    // Enable built-in heartbeat and timeout logic here to ensure it's active early
    websocket::stream_base::timeout opt{
        std::chrono::seconds(5),   // Handshake timeout
        std::chrono::seconds(5),   // Idle timeout (Disconnect if no data from client)
        false                      // Disable WebSocket Pings (Enforce App-Level Heartbeat)
    };
    ws_.set_option(opt);
    ws_.binary(true);
    
    // Set decorators
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(http::field::server, "TinyIM Gateway");
        }));
}

void WebsocketSession::Run() {
    // Already accepted in DoAccept -> OnAccept
}

void WebsocketSession::OnAccept(beast::error_code ec) {
    if(ec) {
        spdlog::error("WS Accept failed: {}", ec.message());
        return;
    }

    ConnectionManager::GetInstance().Join(user_id_, shared_from_this());
    
    // Register Location for gRPC Push (MVP: 127.0.0.1:Port+10000)
    try {
        auto ep = ws_.next_layer().socket().local_endpoint();
        int port = ep.port();
        std::string grpc_addr = "127.0.0.1:" + std::to_string(port + 10000);
        
        std::string key = "im:location:" + std::to_string(user_id_);
        RedisClient::GetInstance().HSet(key, device_, grpc_addr);
        
        // DON'T overwrite im:session - it contains the token for authentication!
        // ChatServer should use im:location to find user's grpc address

        spdlog::info("Registered Location & Session: user={} dev={} addr={}", user_id_, device_, grpc_addr);
    } catch(...) {
        spdlog::error("Failed to get local endpoint or register location");
    }
    
    // Read message
    DoRead();
}

void WebsocketSession::DoRead() {
    ws_.async_read(buffer_,
        beast::bind_front_handler(&WebsocketSession::OnRead, shared_from_this()));
}

#include "service_registry.h" // Added
#include "grpc_channel_pool.h"
#include "config.h"

// gRPC Client Helper
// gRPC Client Helper
static std::unique_ptr<tinyim::chat::ChatService::Stub> GetChatStub() {
    // Dynamic Discovery via Config or ServiceRegistry
    // For MVP use Config, default to localhost:50052
    std::string addr = Config::GetInstance().GetString("chat_service.addr", "127.0.0.1:50052");
    
    // Use Channel Pool
    auto channel = GRPCChannelPool::GetInstance().GetChannel(addr);
    return tinyim::chat::ChatService::NewStub(channel);
}

// Relation Client Helper - Moved to outer scope or forward declared
// Relation Client Helper - Moved to outer scope or forward declared
static std::unique_ptr<tinyim::relation::RelationService::Stub> GetRelationStub() {
    std::string addr = Config::GetInstance().GetString("relation_service.addr", "127.0.0.1:50053");
    auto channel = GRPCChannelPool::GetInstance().GetChannel(addr);
    return tinyim::relation::RelationService::NewStub(channel);
}

void WebsocketSession::OnRead(beast::error_code ec, std::size_t bytes_transferred) {
    if(ec == websocket::error::closed || ec == http::error::end_of_stream) {
        spdlog::info("WS Closed (user={})", user_id_);
        RedisClient::GetInstance().HDel("im:location:" + std::to_string(user_id_), device_);
        ConnectionManager::GetInstance().Leave(user_id_, shared_from_this());
        return;
    }
    if(ec) {
        spdlog::error("WS Read failed: {}", ec.message());
        RedisClient::GetInstance().HDel("im:location:" + std::to_string(user_id_), device_);
        ConnectionManager::GetInstance().Leave(user_id_, shared_from_this());
        return;
    }

    // Append received data to internal buffer is handled by beast buffer
    
    // Process loop
    while (buffer_.size() >= sizeof(PacketHeader)) {
        const char* data_ptr = static_cast<const char*>(buffer_.data().data());
        PacketHeader header;
        std::memcpy(&header, data_ptr, sizeof(PacketHeader));
        
        if (header.magic[0] != 'I' || header.magic[1] != 'M') {
            spdlog::error("Invalid Magic, Closing");
            Close();
            return;
        }

        uint32_t body_len = ntohl(header.body_len);
        if (buffer_.size() < sizeof(PacketHeader) + body_len) {
            // Wait for more data
            break; 
        }

        // Extract Body
        std::string body(data_ptr + sizeof(PacketHeader), body_len);
        uint16_t cmd_id = ntohs(header.cmd_id);
        
        spdlog::info("Recv Packet: User={} Cmd={} Len={}", user_id_, cmd_id, body_len);

        // Handle Commands
        if (cmd_id == CMD_MSG_SEND_REQ) {
            tinyim::chat::SendMessageReq req;
            if (req.ParseFromString(body)) {
                // Override sender_id with session user_id (Security)
                req.set_sender_id(user_id_);
                
                grpc::ClientContext context;
                tinyim::chat::SendMessageResp resp;
                Status status = GetChatStub()->SendMessage(&context, req, &resp);
                
                // Reply to Client
                tinyim::chat::SendMessageResp client_resp = resp;
                if (!status.ok()) {
                    client_resp.set_success(false);
                    client_resp.set_error_message(status.error_message());
                }
                
                std::string resp_body;
                client_resp.SerializeToString(&resp_body);
                SendPacket(CMD_MSG_SEND_RESP, resp_body);
                
            } else {
                spdlog::warn("Parse SendMsgReq failed");
            }
        }
        else if (cmd_id == CMD_FRIEND_APPLY_REQ) {
            tinyim::relation::ApplyFriendReq req;
            if(req.ParseFromString(body)) {
                req.set_user_id(user_id_);
                grpc::ClientContext ctx; tinyim::relation::ApplyFriendResp resp;
                GetRelationStub()->ApplyFriend(&ctx, req, &resp);
                std::string b; resp.SerializeToString(&b);
                SendPacket(CMD_FRIEND_APPLY_RESP, b);
            }
        }
        else if (cmd_id == CMD_FRIEND_ACCEPT_REQ) {
            tinyim::relation::AcceptFriendReq req;
            if(req.ParseFromString(body)) {
                req.set_user_id(user_id_); // Acceptor
                grpc::ClientContext ctx; tinyim::relation::AcceptFriendResp resp;
                GetRelationStub()->AcceptFriend(&ctx, req, &resp);
                std::string b; resp.SerializeToString(&b);
                SendPacket(CMD_FRIEND_ACCEPT_RESP, b);
            }
        }
        else if (cmd_id == CMD_FRIEND_LIST_REQ) {
            tinyim::relation::GetFriendListReq req;
            req.set_user_id(user_id_);
            grpc::ClientContext ctx; tinyim::relation::GetFriendListResp resp;
            GetRelationStub()->GetFriendList(&ctx, req, &resp);
            std::string b; resp.SerializeToString(&b);
            SendPacket(CMD_FRIEND_LIST_RESP, b);
        }
        // --- Group ---
        else if (cmd_id == CMD_GROUP_CREATE_REQ) {
            tinyim::relation::CreateGroupReq req;
            if(req.ParseFromString(body)) {
                req.set_owner_id(user_id_);
                grpc::ClientContext ctx; tinyim::relation::CreateGroupResp resp;
                GetRelationStub()->CreateGroup(&ctx, req, &resp);
                std::string b; resp.SerializeToString(&b);
                SendPacket(CMD_GROUP_CREATE_RESP, b);
            }
        }
        else if (cmd_id == CMD_GROUP_JOIN_REQ) {
            tinyim::relation::JoinGroupReq req;
            if(req.ParseFromString(body)) {
                req.set_user_id(user_id_);
                grpc::ClientContext ctx; tinyim::relation::JoinGroupResp resp;
                GetRelationStub()->JoinGroup(&ctx, req, &resp);
                std::string b; resp.SerializeToString(&b);
                SendPacket(CMD_GROUP_JOIN_RESP, b);
            }
        }
        else if (cmd_id == CMD_GROUP_LIST_REQ) {
            tinyim::relation::GetGroupListReq req;
            req.set_user_id(user_id_);
            grpc::ClientContext ctx; tinyim::relation::GetGroupListResp resp;
            GetRelationStub()->GetGroupList(&ctx, req, &resp);
            std::string b; resp.SerializeToString(&b);
            SendPacket(CMD_GROUP_LIST_RESP, b);
        }
        else if (cmd_id == CMD_GROUP_APPLY_REQ) {
            tinyim::relation::ApplyGroupReq req;
            if(req.ParseFromString(body)) {
                req.set_user_id(user_id_);
                grpc::ClientContext ctx; tinyim::relation::ApplyGroupResp resp;
                GetRelationStub()->ApplyGroup(&ctx, req, &resp);
                std::string b; resp.SerializeToString(&b);
                SendPacket(CMD_GROUP_APPLY_RESP, b);
            }
        }
        else if (cmd_id == CMD_GROUP_ACCEPT_REQ) {
            tinyim::relation::AcceptGroupReq req;
            if(req.ParseFromString(body)) {
                req.set_user_id(user_id_);
                grpc::ClientContext ctx; tinyim::relation::AcceptGroupResp resp;
                GetRelationStub()->AcceptGroup(&ctx, req, &resp);
                std::string b; resp.SerializeToString(&b);
                SendPacket(CMD_GROUP_ACCEPT_RESP, b);
            }
        }
        else if (cmd_id == CMD_MSG_SYNC_REQ) {
            tinyim::chat::SyncMessagesReq req;
            if (req.ParseFromString(body)) {
                req.set_user_id(user_id_);
                
                grpc::ClientContext context;
                tinyim::chat::SyncMessagesResp resp;
                Status status = GetChatStub()->SyncMessages(&context, req, &resp);
                 
                // Reply
                std::string resp_body;
                if (status.ok()) {
                     resp.SerializeToString(&resp_body);
                } else {
                     // Empty resp with success=false
                     tinyim::chat::SyncMessagesResp err_resp;
                     err_resp.set_success(false);
                     err_resp.SerializeToString(&resp_body);
                }
                SendPacket(CMD_MSG_SYNC_RESP, resp_body);
            }
        } else if (cmd_id == CMD_HEARTBEAT_REQ) {
            // Reply Heartbeat
            SendPacket(CMD_HEARTBEAT_RESP, "");
        } else if (cmd_id == CMD_LOGIN_REQ) {
            // Already authenticated via HTTP Upgrade. 
            // Respond with SuccessIDempotently.
            // We can parse body to check if they sent token again, but unnecessary.
            
            // Construct LoginResp (Proto not included? Hand-craft or include proto)
            // Ideally we need auth.pb.h
            // For MVP, empty body or simple JSON?
            // Let's just send CMD_LOGIN_RESP with empty body (Success)
            SendPacket(CMD_LOGIN_RESP, ""); 
        } 
        
        // Consume processed data
        buffer_.consume(sizeof(PacketHeader) + body_len);
    }
    
    DoRead();
} // Close OnRead function

void WebsocketSession::SendPacket(uint16_t cmd_id, const std::string& body) {
    PacketHeader header;
    header.magic[0] = 'I'; header.magic[1] = 'M';
    header.version = 1;
    header.cmd_id = htons(cmd_id);
    header.body_len = htonl(body.size());
    
    std::string packet;
    packet.resize(sizeof(PacketHeader) + body.size());
    memcpy(&packet[0], &header, sizeof(PacketHeader));
    if (!body.empty()) {
        memcpy(&packet[sizeof(PacketHeader)], body.data(), body.size());
    }
    Send(packet);
}

void WebsocketSession::DoWrite() {
    // Must be called inside strand
    if (is_writing_ || write_queue_.empty()) return;
    
    is_writing_ = true;
    auto& msg = write_queue_.front();
    
    ws_.async_write(boost::asio::buffer(msg),
        boost::asio::bind_executor(strand_,
            beast::bind_front_handler(&WebsocketSession::OnWrite, shared_from_this())));
}

void WebsocketSession::OnWrite(beast::error_code ec, std::size_t bytes_transferred) {
    if (ec) {
        spdlog::error("WS Write failed: {}", ec.message());
        // Should we close? Ideally yes.
        // Close();
        // Clear queue?
    }
    
    // Remove sent message
    if (!write_queue_.empty()) write_queue_.pop();
    is_writing_ = false;
    
    // Continue
    if (!write_queue_.empty() && !ec) {
        DoWrite();
    } else if (write_queue_.empty() && close_after_write_) {
        // Queue drained and kick requested
        spdlog::info("Write queue drained, closing session (Kick)");
        Close();
    }
}

void WebsocketSession::Kick() {
    boost::asio::post(strand_, [self = shared_from_this()]() {
        // Construct Kick Packet
        PacketHeader header;
        header.magic[0] = 'I'; header.magic[1] = 'M';
        header.version = 1;
        header.cmd_id = htons(CMD_LOGOUT_RESP);
        
        std::string body = "Kicked by new login";
        header.body_len = htonl(body.size());
        
        std::string packet;
        packet.resize(sizeof(header) + body.size());
        memcpy(&packet[0], &header, sizeof(header));
        memcpy(&packet[sizeof(header)], body.data(), body.size());
        
        self->write_queue_.push(packet);
        self->close_after_write_ = true;
        
        self->DoWrite();
    });
}

void WebsocketSession::Send(const std::string& msg) {
    boost::asio::post(strand_, [self = shared_from_this(), msg]() {
        self->write_queue_.push(msg);
        self->DoWrite();
    });
}

void WebsocketSession::Close() {
    ws_.async_close(websocket::close_code::normal,
        [self = shared_from_this()](beast::error_code ec) {});
}
