#include "chat_service_impl.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <grpcpp/grpcpp.h>
#include "gateway.grpc.pb.h"

Status ChatServiceImpl::SendMessage(ServerContext* context, const tinyim::chat::SendMessageReq* request,
                                    tinyim::chat::SendMessageResp* reply) {
    int64_t sender_id = request->sender_id();
    int64_t receiver_id = request->receiver_id();
    std::string content = request->content();



    DBConn conn; // Write Connection
    DBConn read_conn(DBConn::READ); // Read Connection
    
    if (!conn.valid() || !read_conn.valid()) return Status(grpc::INTERNAL, "Database Error");

    // Escape Content
    std::vector<char> escaped_buffer(content.length() * 2 + 1);
    mysql_real_escape_string(conn.get(), escaped_buffer.data(), content.c_str(), content.length());
    std::string safe_content = escaped_buffer.data();
    
    int64_t group_id = request->group_id();
    int type = (int)request->type();

    // --- 1. Store Message Body (Write Once) ---
    // In production, use Snowflake ID. Here use auto-increment from DB.
    std::string sql_body = "INSERT INTO im_message_body (sender_id, group_id, msg_type, msg_content) VALUES (" + 
                           std::to_string(sender_id) + ", " + 
                           std::to_string(group_id) + ", " + 
                           std::to_string(type) + ", '" + safe_content + "')";
    
    if (mysql_query(conn.get(), sql_body.c_str())) {
        spdlog::error("Insert Body Failed: {}", mysql_error(conn.get()));
        reply->set_success(false);
        reply->set_error_message("Save Body Failed");
        return Status::OK;
    }
    int64_t msg_id = mysql_insert_id(conn.get());

    // --- 3. Store Inbox Index (Timeline) ---
    // Handle Group or Single
    if (group_id > 0) {
        // Group Chat: Write Diffusion (Fan-out)
        // 1. Get All Members
        std::vector<int64_t> members;
        std::string sql_mem = "SELECT user_id FROM im_group_member WHERE group_id=" + std::to_string(group_id);
        if (mysql_query(read_conn.get(), sql_mem.c_str())) {
             spdlog::error("Get Group Members Failed: {}", mysql_error(read_conn.get()));
        } else {
             MYSQL_RES* res = mysql_store_result(read_conn.get());
             if (res) {
                 MYSQL_ROW row;
                 while ((row = mysql_fetch_row(res))) {
                     members.push_back(std::stoll(row[0]));
                 }
                 mysql_free_result(res);
             }
        }
        
        // 2. Loop Insert & Push (Optimize: Batch Insert?)
        // For MVP: Loop
        for (int64_t member_uid : members) {
             // In Group Chat, other_id is typically the GroupID or SenderID?
             // Usually for Group timeline, we query by owner_id.
             // But UI needs to know which group.
             // The `MessageItem` has `group_id`.
             // `im_message_index`: owner_id=Member, other_id=Group, is_sender=0
             
             // Seq: Each user has their own timeline seq
             std::string m_seq_key = "im:seq:" + std::to_string(member_uid);
             long long m_seq = 0;
             RedisConn r_conn; // New scope
             if (r_conn.get()) {
                 redisReply* r = (redisReply*)redisCommand(r_conn.get(), "INCR %s", m_seq_key.c_str());
                 if (r && r->type == REDIS_REPLY_INTEGER) m_seq = r->integer;
                 if (r) freeReplyObject(r);
             }
             
             std::string q = "INSERT INTO im_message_index (owner_id, other_id, msg_id, seq_id, is_sender) VALUES (" +
                             std::to_string(member_uid) + ", " + std::to_string(group_id) + ", " + 
                             std::to_string(msg_id) + ", " + std::to_string(m_seq) + ", 0)";
             mysql_query(conn.get(), q.c_str());
             
             // Push
             // PushNotify logic (extract to function)
             bool m_online = RedisClient::GetInstance().Exists("im:session:" + std::to_string(member_uid));
             if (m_online) {
                 // Copy Paste Push Logic (Better Refactor)
                 std::string loc_key = "im:location:" + std::to_string(member_uid);
                 auto locations = RedisClient::GetInstance().HGetAll(loc_key);
                 for (auto& kv : locations) {
                    try {
                        auto channel = grpc::CreateChannel(kv.second, grpc::InsecureChannelCredentials());
                        auto stub = tinyim::gateway::GatewayService::NewStub(channel);
                        grpc::ClientContext ctx;
                        tinyim::gateway::PushNotifyReq pr;
                        pr.set_user_id(member_uid); pr.set_max_seq(m_seq); pr.set_msg_type(request->type());
                        tinyim::gateway::PushNotifyResp presp;
                        stub->PushNotify(&ctx, pr, &presp);
                    } catch(...) {}
                 }
             }
        }
        
        // Reply to Sender
        reply->set_msg_id(msg_id);
        reply->set_seq_id(0); // Group msg seq is per-user, sender doesn't need single seq
        reply->set_success(true);
        return Status::OK;
        
    } else {
        // Single Chat (Existing Logic)
        
        // --- RELATION CHECK (Check if they are friends) ---
        // We select status from im_relation
        // status 1 = normal, 2 = block
        std::string sql_rel = "SELECT status FROM im_relation WHERE user_id=" + std::to_string(sender_id) + 
                              " AND friend_id=" + std::to_string(receiver_id);
        
        bool is_friend = false;
        if (mysql_query(read_conn.get(), sql_rel.c_str()) == 0) {
             MYSQL_RES* res = mysql_store_result(read_conn.get());
             if (res) {
                 MYSQL_ROW row = mysql_fetch_row(res);
                 if (row && std::stol(row[0]) == 1) {
                     is_friend = true;
                 }
                 mysql_free_result(res);
             }
        }
        
        // Allow SYSTEM messages (type=3) or Friend Request (type=4) even if not friend
        if (!is_friend && type != 3 && type != 4) {
             reply->set_success(false);
             reply->set_error_message("Not friends");
             return Status::OK;
        }

        // Get Seq
        std::string seq_key = "im:seq:" + std::to_string(receiver_id);
        long long seq_id = 0;
        
        RedisConn redis_conn; // Declare here
        if (redis_conn.get()) {
            redisReply* r_seq = (redisReply*)redisCommand(redis_conn.get(), "INCR %s", seq_key.c_str());
            if (r_seq && r_seq->type == REDIS_REPLY_INTEGER) seq_id = r_seq->integer;
            if (r_seq) freeReplyObject(r_seq);
        }

        std::string sql_index = "INSERT INTO im_message_index (owner_id, other_id, msg_id, seq_id, is_sender) VALUES (" +
                                std::to_string(receiver_id) + ", " + std::to_string(sender_id) + ", " + 
                                std::to_string(msg_id) + ", " + std::to_string(seq_id) + ", 0)";
        
        if (mysql_query(conn.get(), sql_index.c_str())) {
            spdlog::error("Insert Index Receiver Failed: {}", mysql_error(conn.get()));
            reply->set_success(false);
            return Status::OK;
        }
        
        // Push
        std::string status_key = "im:session:" + std::to_string(receiver_id);
        if (RedisClient::GetInstance().Exists(status_key)) {
             std::string loc_key = "im:location:" + std::to_string(receiver_id);
             auto locations = RedisClient::GetInstance().HGetAll(loc_key);
             for (auto& kv : locations) {
                try {
                    auto channel = grpc::CreateChannel(kv.second, grpc::InsecureChannelCredentials());
                    auto stub = tinyim::gateway::GatewayService::NewStub(channel);
                    grpc::ClientContext ctx;
                    tinyim::gateway::PushNotifyReq pr;
                    pr.set_user_id(receiver_id); pr.set_max_seq(seq_id); pr.set_msg_type(request->type());
                    tinyim::gateway::PushNotifyResp presp;
                    stub->PushNotify(&ctx, pr, &presp);
                } catch(...) {}
             }
        }
        
        reply->set_msg_id(msg_id);
        reply->set_seq_id(seq_id);
        reply->set_success(true);
        return Status::OK;
    }
}

Status ChatServiceImpl::SyncMessages(ServerContext* context, const tinyim::chat::SyncMessagesReq* request,
                                     tinyim::chat::SyncMessagesResp* reply) {
    int64_t user_id = request->user_id();
    int64_t local_seq = request->local_seq();
    int limit = request->limit();
    if (limit <= 0) limit = 10;

    DBConn conn(DBConn::READ);
    
    // JOIN query to get content
    // SELECT idx.seq_id, idx.msg_id, idx.other_id, body.msg_content, body.created_at
    // FROM im_message_index idx
    // LEFT JOIN im_message_body body ON idx.msg_id = body.msg_id
    // WHERE idx.owner_id = ? AND idx.seq_id > ?
    // ORDER BY idx.seq_id ASC LIMIT ?

    bool reverse = request->reverse();
    
    char sql[512];
    if (reverse) {
        // Web Mode: Pull latest N messages.
        snprintf(sql, sizeof(sql), 
                 "SELECT idx.seq_id, idx.msg_id, body.sender_id, body.group_id, body.msg_type, body.msg_content, body.created_at "
                 "FROM im_message_index idx "
                 "LEFT JOIN im_message_body body ON idx.msg_id = body.msg_id "
                 "WHERE idx.owner_id=%ld "
                 "ORDER BY idx.seq_id DESC LIMIT %d",
                 user_id, limit);
    } else {
        // PC Mode: Resume from local_seq
        snprintf(sql, sizeof(sql), 
                 "SELECT idx.seq_id, idx.msg_id, body.sender_id, body.group_id, body.msg_type, body.msg_content, body.created_at "
                 "FROM im_message_index idx "
                 "LEFT JOIN im_message_body body ON idx.msg_id = body.msg_id "
                 "WHERE idx.owner_id=%ld AND idx.seq_id > %ld "
                 "ORDER BY idx.seq_id ASC LIMIT %d",
                 user_id, local_seq, limit);
    }

    if (mysql_query(conn.get(), sql)) {
        reply->set_success(false);
        spdlog::error("Sync Query Failed: {}", mysql_error(conn.get()));
        return Status::OK;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    if (!res) {
        reply->set_success(true);
        return Status::OK;
    }

    MYSQL_ROW row;
    int64_t max_seq_found = local_seq;
    while ((row = mysql_fetch_row(res))) {
        auto* msg = reply->add_msgs();
        int64_t s_id = std::stoll(row[0]);
        msg->set_seq_id(s_id);
        msg->set_msg_id(std::stoll(row[1]));
        msg->set_sender_id(std::stoll(row[2])); // Real sender_id from body
        msg->set_group_id(std::stoll(row[3]));  // Real group_id from body
        msg->set_type((tinyim::chat::MsgType)std::stoi(row[4]));
        msg->set_content(row[5] ? row[5] : "");
        msg->set_created_at(row[6] ? row[6] : "");
        
        if (s_id > max_seq_found) max_seq_found = s_id;
    }
    mysql_free_result(res);

    reply->set_max_seq(max_seq_found);
    reply->set_success(true);
    return Status::OK;
}
