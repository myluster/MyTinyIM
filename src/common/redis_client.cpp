#include "redis_client.h"

RedisClient& RedisClient::GetInstance() {
    static RedisClient instance;
    return instance;
}

void RedisClient::Init(const std::string& host, int port) {
    host_ = host;
    port_ = port;
}

redisContext* RedisClient::GetContext() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!pool_.empty()) {
            redisContext* ctx = pool_.back();
            pool_.pop_back();
            return ctx;
        }
    }
    
    // Create new
    redisContext* c = redisConnect(host_.c_str(), port_);
    if (c == nullptr || c->err) {
        if (c) {
            spdlog::error("Redis connection error: {}", c->errstr);
            redisFree(c);
        } else {
            spdlog::error("Redis connection error: can't allocate context");
        }
        return nullptr;
    }
    return c;
}

void RedisClient::ReleaseContext(redisContext* ctx) {
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(mtx_);
    pool_.push_back(ctx);
}

RedisClient::~RedisClient() {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto c : pool_) {
        redisFree(c);
    }
    pool_.clear();
}

bool RedisClient::Set(const std::string& key, const std::string& value) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "SET %s %s", key.c_str(), value.c_str());
    if (!reply) return false;
    bool success = (reply->type != REDIS_REPLY_ERROR);
    freeReplyObject(reply);
    return success;
}

bool RedisClient::SetEx(const std::string& key, const std::string& value, int seconds) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "SET %s %s EX %d", key.c_str(), value.c_str(), seconds);
    bool ret = (reply && reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
    if (reply) freeReplyObject(reply);
    return ret;
}

std::vector<std::string> RedisClient::Keys(const std::string& pattern) {
    std::vector<std::string> result;
    RedisConn conn;
    if (!conn.get()) return result;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "KEYS %s", pattern.c_str());
    if (reply) {
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; i++) {
                if(reply->element[i]->str) {
                    result.push_back(reply->element[i]->str);
                }
            }
        }
        freeReplyObject(reply);
    }
    return result;
}

std::string RedisClient::Get(const std::string& key) {
    RedisConn conn;
    if (!conn.get()) return "";
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "GET %s", key.c_str());
    if (!reply) return "";
    std::string res;
    if (reply->type == REDIS_REPLY_STRING) {
        res = reply->str;
    }
    freeReplyObject(reply);
    return res;
}

bool RedisClient::Del(const std::string& key) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "DEL %s", key.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

bool RedisClient::Exists(const std::string& key) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "EXISTS %s", key.c_str());
    bool exists = false;
    if (reply) {
        exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
        freeReplyObject(reply);
    }
    return exists;
}

bool RedisClient::Expire(const std::string& key, int seconds) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "EXPIRE %s %d", key.c_str(), seconds);
    freeReplyObject(reply);
    return true;
}

bool RedisClient::HSet(const std::string& key, const std::string& field, const std::string& value) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

std::string RedisClient::HGet(const std::string& key, const std::string& field) {
    RedisConn conn;
    if (!conn.get()) return "";
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "HGET %s %s", key.c_str(), field.c_str());
    if (!reply) return "";
    std::string res;
    if (reply->type == REDIS_REPLY_STRING) {
        res = reply->str;
    }
    freeReplyObject(reply);
    return res;
}

bool RedisClient::HDel(const std::string& key, const std::string& field) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "HDEL %s %s", key.c_str(), field.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

std::unordered_map<std::string, std::string> RedisClient::HGetAll(const std::string& key) {
    std::unordered_map<std::string, std::string> res;
    RedisConn conn;
    if (!conn.get()) return res;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "HGETALL %s", key.c_str());
    if (reply) {
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; i += 2) {
                if (i+1 < reply->elements && reply->element[i]->str && reply->element[i+1]->str) {
                    res[reply->element[i]->str] = reply->element[i+1]->str;
                }
            }
        }
        freeReplyObject(reply);
    }
    return res;
}

bool RedisClient::Publish(const std::string& channel, const std::string& message) {
    RedisConn conn;
    if (!conn.get()) return false;
    redisReply* reply = (redisReply*)redisCommand(conn.get(), "PUBLISH %s %s", channel.c_str(), message.c_str());
    if (!reply) return false;
    freeReplyObject(reply);
    return true;
}

void RedisClient::Subscribe(const std::string& channel, std::function<void(const std::string&)> callback) {
    // Need a raw connection that is NOT returned to the pool because it enters subscribe mode
    redisContext* ctx = redisConnect(host_.c_str(), port_);
    if (!ctx || ctx->err) {
        spdlog::error("Subscribe connect failed");
        return;
    }
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "SUBSCRIBE %s", channel.c_str());
    if (reply) freeReplyObject(reply);
    
    while(redisGetReply(ctx, (void**)&reply) == REDIS_OK) {
        // [type, channel, message]
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {
             // element[0] is "message"
             // element[1] is channel name
             // element[2] is the payload
             if (reply->element[2]->str) {
                 if (callback) callback(reply->element[2]->str);
             }
        }
        freeReplyObject(reply);
    }
    redisFree(ctx);
}
