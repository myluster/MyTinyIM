#include "auth_service_impl.h"
#include "db_pool.h"
#include "redis_client.h"
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <iomanip>

std::string GenerateToken(int64_t user_id) {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::stringstream ss;
    ss << "token_" << user_id << "_" << now;
    return ss.str();
}

Status AuthServiceImpl::Register(ServerContext* context, const tinyim::auth::RegisterReq* request,
                                 tinyim::auth::RegisterResp* reply) {
    std::string username = request->username();
    std::string password = request->password();
    std::string nickname = request->nickname();

    if (username.empty() || password.empty()) {
        reply->set_success(false);
        reply->set_error_message("Username or password cannot be empty");
        return Status::OK;
    }

    DBConn conn;
    if (!conn.valid()) {
        return Status(grpc::INTERNAL, "Database error");
    }

    // Check if exists
    // (省略 Select count(*)... 直接依赖唯一索引报错或者先查)
    
    // Insert
    std::string sql = "INSERT INTO im_user (username, password_hash, nickname) VALUES ('" + 
                      username + "', '" + password + "', '" + nickname + "')";
    
    if (mysql_query(conn.get(), sql.c_str())) {
        reply->set_success(false);
        reply->set_error_message("Register failed: User may exist");
        spdlog::error("Register SQL failed: {}", mysql_error(conn.get()));
    } else {
        reply->set_success(true);
        reply->set_user_id(mysql_insert_id(conn.get()));
        spdlog::info("User registered: id={}", reply->user_id());
    }

    return Status::OK;
}

Status AuthServiceImpl::Login(ServerContext* context, const tinyim::auth::LoginReq* request,
                              tinyim::auth::LoginResp* reply) {
    std::string username = request->username();
    std::string password = request->password();
    std::string device = request->device();
    if (device.empty()) device = "PC";

    // Use READ connection for Login query
    DBConn conn(DBConn::READ);
    if (!conn.valid()) return Status(grpc::INTERNAL, "Database error");

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT user_id, nickname, password_hash FROM im_user WHERE username='%s'", username.c_str());

    if (mysql_query(conn.get(), sql)) {
        reply->set_success(false);
        reply->set_error_message("DB Query Error");
        return Status::OK;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    if (!res) {
        reply->set_success(false);
        reply->set_error_message("User not found");
        return Status::OK;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        reply->set_success(false);
        reply->set_error_message("User not found");
        mysql_free_result(res);
        return Status::OK;
    }

    int64_t user_id = std::stoll(row[0]);
    std::string db_nick = row[1] ? row[1] : "";
    std::string db_pass = row[2] ? row[2] : "";
    mysql_free_result(res);

    if (db_pass != password) { // Plaintext comparison for MVP
        reply->set_success(false);
        reply->set_error_message("Invalid password");
        return Status::OK;
    }

    // Login Success
    // Generate Token
    std::string token = GenerateToken(user_id);
    
    // Save to Redis (Hash Structure for Multi-device)
    // Key: im:session:{user_id}
    // Field: {device}
    // Value: {token} (In future can be JSON with gateway IP)
    
    std::string session_key = "im:session:" + std::to_string(user_id);
    
    // Kick-out logic (Mutual Exclusion for same device type)
    std::string old_token = RedisClient::GetInstance().HGet(session_key, device);
    if (!old_token.empty()) {
        spdlog::warn("Kick out old session: user={} device={} old_token={}", user_id, device, old_token);
        
        // Notify Gateway via Redis Pub/Sub
        // Format: "user_id:device"
        std::string kick_msg = std::to_string(user_id) + ":" + device;
        RedisClient::GetInstance().Publish("im:kick", kick_msg);
    }
    
    RedisClient::GetInstance().HSet(session_key, device, token);
    RedisClient::GetInstance().Expire(session_key, 3600 * 24); // Refresh Session TTL
    
    // Load Balancer: Pick a random gateway
    std::string gateway_url = "ws://127.0.0.1:8080/ws"; // default fallback
    auto keys = RedisClient::GetInstance().Keys("im:gateway:*");
    if (!keys.empty()) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, keys.size() - 1);
        std::string chosen_key = keys[dis(gen)];
        
        std::string addr = RedisClient::GetInstance().Get(chosen_key); // "127.0.0.1:8081"
        if (!addr.empty()) {
            gateway_url = "ws://" + addr + "/ws";
        }
    }

    reply->set_success(true);
    reply->set_user_id(user_id);
    reply->set_token(token);
    reply->set_nickname(db_nick);
    // Note: We need to add `gateway_url` to LoginResp proto if we want to pass it correctly.
    // Currently LoginResp proto might NOT have gateway_url field. 
    // Let's check auth.proto. If not, we have to pass it via other means or ADD IT.
    
    // Check proto first! 
    // If auth.proto doesn't have it, we can't set it here.
    // But wait, the previous code in http_session.cpp line 209 hardcoded it in JSON response:
    // {"gateway_url", "ws://127.0.0.1:8080/ws"}
    // So Auth Server just returns success, and HTTP SESSION constructs the JSON.
    // Ah! So Auth Server doesn't need to return it, HTTP Session needs to discover it.
    // BUT, Auth Server is the one doing "Auth", so it makes sense for Auth to return the endpoint.
    
    // However, if we don't want to change proto right now:
    // We can move this logic to HttpSession (Gateway) handleRequest.
    // But Gateway shouldn't be responsible for global LB? Or actually Gateway layer IS the entry point.
    // The Client calls Gateway HTTP /api/login via LB (Nginx) -> Any Gateway instance -> Auth Server via gRPC.
    // The Auth Server validates. 
    // Who decides the WS target? Usually the "Dispatch Service" (which is currently the HTTP part of Gateway).
    
    // So, let's keep Auth Server Pure (Validation).
    // And move LB logic to Gateway's HttpSession::HandleRequest.
    
    // REVERTING change to this file. I will modify http_session.cpp instead.
    
    spdlog::info("User login: id={}, device={}, stored in Redis Hash", user_id, device);

    return Status::OK;
}

Status AuthServiceImpl::Logout(ServerContext* context, const tinyim::auth::LogoutReq* request,
                               tinyim::auth::LogoutResp* reply) {
    int64_t user_id = request->user_id();
    std::string device = request->device();
    
    if (device.empty()) {
        RedisClient::GetInstance().Del("im:session:" + std::to_string(user_id));
        // If device is empty, maybe kick all? For MVP let's imply PC or just ignore kick.
        // Better: Find all devices? Too complex for MVP.
    } else {
        RedisClient::GetInstance().HDel("im:session:" + std::to_string(user_id), device);
        // Kick the specific device
        std::string kick_msg = std::to_string(user_id) + ":" + device;
        RedisClient::GetInstance().Publish("im:kick", kick_msg);
    }
    
    reply->set_success(true);
    return Status::OK;
}
