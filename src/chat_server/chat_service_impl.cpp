#include "chat_service_impl.h"
#include <spdlog/spdlog.h>
#include <sstream>

Status ChatServiceImpl::SendMessage(ServerContext* context, const tinyim::chat::SendMessageReq* request,
                                    tinyim::chat::SendMessageResp* reply) {
    int64_t sender_id = request->sender_id();
    int64_t receiver_id = request->receiver_id();
    std::string content = request->content();

    DBConn conn;
    if (!conn.valid()) return Status(grpc::INTERNAL, "Database Error");
    
    // --- 1. Store Message Body (Write Once) ---
    // In production, use Snowflake ID. Here use auto-increment from DB.
    std::string sql_body = "INSERT INTO im_message_body (sender_id, msg_content) VALUES (" + 
                           std::to_string(sender_id) + ", '" + content + "')";
    
    if (mysql_query(conn.get(), sql_body.c_str())) {
        spdlog::error("Insert Body Failed: {}", mysql_error(conn.get()));
        reply->set_success(false);
        reply->set_error_message("Save Body Failed");
        return Status::OK;
    }
    int64_t msg_id = mysql_insert_id(conn.get());

    // --- 2. Get Next Sequence ID for Receiver ---
    // Redis INCR: im:seq:{receiver_id}
    std::string seq_key = "im:seq:" + std::to_string(receiver_id);
    RedisConn redis_conn;
    if (!redis_conn.get()) return Status(grpc::INTERNAL, "Redis Error");
    
    long long seq_id = 0;
    redisReply* r_seq = (redisReply*)redisCommand(redis_conn.get(), "INCR %s", seq_key.c_str());
    if (r_seq && r_seq->type == REDIS_REPLY_INTEGER) {
        seq_id = r_seq->integer;
    }
    if (r_seq) freeReplyObject(r_seq);

    if (seq_id == 0) {
        reply->set_success(false);
        reply->set_error_message("Seq Gen Failed");
        return Status::OK;
    }

    // --- 3. Store Inbox Index (Timeline) ---
    // Owner=Receiver, Other=Sender
    std::string sql_index = "INSERT INTO im_message_index (owner_id, other_id, msg_id, seq_id, is_sender) VALUES (" +
                            std::to_string(receiver_id) + ", " + std::to_string(sender_id) + ", " + 
                            std::to_string(msg_id) + ", " + std::to_string(seq_id) + ", 0)";
    
    if (mysql_query(conn.get(), sql_index.c_str())) {
        spdlog::error("Insert Index Receiver Failed: {}", mysql_error(conn.get()));
        // Note: transaction rollback would be needed in real prod
        reply->set_success(false);
        reply->set_error_message("Save Index Failed");
        return Status::OK;
    }

    // --- Optional: Store Sender's Outbox Index (for multi-device sync) ---
    // Skip for MVP, but good practice.

    // --- 4. Push Notification if Online ---
    // Check if ANY device is online (Has 'im:session:{uid}' key)
    std::string status_key = "im:session:" + std::to_string(receiver_id);
    bool is_online = RedisClient::GetInstance().Exists(status_key);
    
    spdlog::info("Msg Saved: ID={} Seq={}. Receiver Online? {}", msg_id, seq_id, is_online);

    if (is_online) {
        // Mock Push: In real world, send gRPC to Gateway
        spdlog::info(">>> PUSH Signal sent to Receiver {}", receiver_id);
    }

    reply->set_msg_id(msg_id);
    reply->set_seq_id(seq_id);
    reply->set_success(true);

    return Status::OK;
}

Status ChatServiceImpl::SyncMessages(ServerContext* context, const tinyim::chat::SyncMessagesReq* request,
                                     tinyim::chat::SyncMessagesResp* reply) {
    int64_t user_id = request->user_id();
    int64_t local_seq = request->local_seq();
    int limit = request->limit();
    if (limit <= 0) limit = 10;

    DBConn conn;
    
    // JOIN query to get content
    // SELECT idx.seq_id, idx.msg_id, idx.other_id, body.msg_content, body.created_at
    // FROM im_message_index idx
    // LEFT JOIN im_message_body body ON idx.msg_id = body.msg_id
    // WHERE idx.owner_id = ? AND idx.seq_id > ?
    // ORDER BY idx.seq_id ASC LIMIT ?

    char sql[512];
    snprintf(sql, sizeof(sql), 
             "SELECT idx.seq_id, idx.msg_id, idx.other_id, body.msg_content, body.created_at "
             "FROM im_message_index idx "
             "LEFT JOIN im_message_body body ON idx.msg_id = body.msg_id "
             "WHERE idx.owner_id=%ld AND idx.seq_id > %ld "
             "ORDER BY idx.seq_id ASC LIMIT %d",
             user_id, local_seq, limit);

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
        msg->set_sender_id(std::stoll(row[2])); // other_id is sender (because is_sender=0)
        msg->set_content(row[3] ? row[3] : "");
        msg->set_created_at(row[4] ? row[4] : "");
        
        if (s_id > max_seq_found) max_seq_found = s_id;
    }
    mysql_free_result(res);

    reply->set_max_seq(max_seq_found);
    reply->set_success(true);
    return Status::OK;
}
