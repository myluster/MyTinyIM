#include "relation_service_impl.h"
#include "db_pool.h"
#include <spdlog/spdlog.h>
#include <sstream>

// Helper to get ChatStub
static std::unique_ptr<tinyim::chat::ChatService::Stub> GetChatStub() {
    static std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel("localhost:50052", grpc::InsecureChannelCredentials());
    return tinyim::chat::ChatService::NewStub(channel);
}

Status RelationServiceImpl::ApplyFriend(ServerContext* context, const tinyim::relation::ApplyFriendReq* request,
                                        tinyim::relation::ApplyFriendResp* reply) {
    int64_t user_id = request->user_id();
    int64_t friend_id = request->friend_id();
    std::string remark = request->remark();

    if (user_id == friend_id) {
        reply->set_success(false);
        reply->set_error_message("Cannot add self");
        return Status::OK;
    }

    DBConn conn;
    if (!conn.valid()) return Status(grpc::INTERNAL, "DB Error");

    // Insert into im_friend_request (Assume table exists or use im_relation with status=0)
    // Let's create `im_friend_request` dynamically if needed? No, code assumes schema.
    // I need to add SQL creation later. For now, write SQL assuming table.
    
    // Check duplicate apply
    std::string check_sql = "SELECT id FROM im_friend_request WHERE user_id=" + std::to_string(user_id) + 
                            " AND friend_id=" + std::to_string(friend_id) + " AND status=0"; // 0=Pending
    if (mysql_query(conn.get(), check_sql.c_str()) == 0) {
         MYSQL_RES* res = mysql_store_result(conn.get());
         if (res && mysql_fetch_row(res)) {
             mysql_free_result(res);
             reply->set_success(false);
             reply->set_error_message("Request already pending");
             return Status::OK;
         }
         if (res) mysql_free_result(res);
    }

    std::string sql = "INSERT INTO im_friend_request (user_id, friend_id, remark) VALUES (" +
                      std::to_string(user_id) + ", " + std::to_string(friend_id) + ", '" + remark + "')";
    
    if (mysql_query(conn.get(), sql.c_str())) {
        spdlog::error("ApplyFriend DB Failed: {}", mysql_error(conn.get()));
        reply->set_success(false);
        reply->set_error_message("Apply failed");
        return Status::OK;
    }
    
    // Send System Msg to Friend
    tinyim::chat::SendMessageReq msg_req;
    msg_req.set_sender_id(user_id); // Or System ID?
    msg_req.set_receiver_id(friend_id);
    msg_req.set_type(tinyim::chat::FRIEND_REQ); // Magic Enum
    msg_req.set_content("Friend Request"); // UI handles logic
    
    grpc::ClientContext ctx;
    tinyim::chat::SendMessageResp msg_resp;
    GetChatStub()->SendMessage(&ctx, msg_req, &msg_resp);

    reply->set_success(true);
    reply->set_apply_id(mysql_insert_id(conn.get()));
    return Status::OK;
}

Status RelationServiceImpl::AcceptFriend(ServerContext* context, const tinyim::relation::AcceptFriendReq* request,
                                         tinyim::relation::AcceptFriendResp* reply) {
    int64_t user_id = request->user_id(); // The one who accepts
    int64_t requester_id = request->requester_id();
    bool accept = request->accept();
    
    DBConn conn;
    
    // Update Request Status (TODO)
    
    if (accept) {
        // Insert Relations (Bidirectional)
        std::stringstream ss;
        ss << "INSERT INTO im_relation (user_id, friend_id, status) VALUES "
           << "(" << user_id << "," << requester_id << ",1), "
           << "(" << requester_id << "," << user_id << ",1)";
        
        if (mysql_query(conn.get(), ss.str().c_str())) {
            // Ignore Duplicate entry
            spdlog::warn("AcceptFriend Insert Relation: {}", mysql_error(conn.get()));
        }
        
        // Notify Requester (System Msg)
        tinyim::chat::SendMessageReq msg_req;
        msg_req.set_receiver_id(requester_id);
        msg_req.set_type(tinyim::chat::SYSTEM);
        msg_req.set_content("Friend Request Accepted"); 
        
        grpc::ClientContext ctx;
        tinyim::chat::SendMessageResp msg_resp;
        GetChatStub()->SendMessage(&ctx, msg_req, &msg_resp);
    }
    
    reply->set_success(true);
    return Status::OK;
}

Status RelationServiceImpl::GetFriendList(ServerContext* context, const tinyim::relation::GetFriendListReq* request,
                                          tinyim::relation::GetFriendListResp* reply) {
    int64_t user_id = request->user_id();
    DBConn conn(DBConn::READ);
    
    std::string sql = "SELECT r.friend_id, u.nickname, u.username FROM im_relation r "
                      "JOIN im_user u ON r.friend_id = u.user_id "
                      "WHERE r.user_id = " + std::to_string(user_id) + " AND r.status = 1";
                      
    if (mysql_query(conn.get(), sql.c_str())) return Status::OK;
    
    MYSQL_RES* res = mysql_store_result(conn.get());
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            auto* item = reply->add_friends();
            item->set_user_id(std::stoll(row[0]));
            item->set_nickname(row[1] ? row[1] : "");
            // row[2] username
        }
        mysql_free_result(res);
    }
    reply->set_success(true);
    return Status::OK;
}

Status RelationServiceImpl::CreateGroup(ServerContext* context, const tinyim::relation::CreateGroupReq* request,
                                        tinyim::relation::CreateGroupResp* reply) {
    int64_t owner_id = request->owner_id();
    std::string name = request->group_name();
    
    DBConn conn;
    std::string sql1 = "INSERT INTO im_group (group_name, owner_id) VALUES ('" + name + "', " + std::to_string(owner_id) + ")";
    if (mysql_query(conn.get(), sql1.c_str())) {
        reply->set_success(false);
        return Status::OK;
    }
    int64_t group_id = mysql_insert_id(conn.get());
    
    // Add Owner and Initial Members
    std::string sql2 = "INSERT INTO im_group_member (group_id, user_id, role) VALUES (" + 
                       std::to_string(group_id) + ", " + std::to_string(owner_id) + ", 2)"; // 2=Owner
                       
    mysql_query(conn.get(), sql2.c_str());
    
    for (auto uid : request->initial_members()) {
        if (uid == owner_id) continue;
        std::string s = "INSERT INTO im_group_member (group_id, user_id, role) VALUES (" + 
                       std::to_string(group_id) + ", " + std::to_string(uid) + ", 1)";
        mysql_query(conn.get(), s.c_str());
    }
    
    reply->set_success(true);
    reply->set_group_id(group_id);
    return Status::OK;
}

Status RelationServiceImpl::JoinGroup(ServerContext* context, const tinyim::relation::JoinGroupReq* request,
                                      tinyim::relation::JoinGroupResp* reply) {
    int64_t user_id = request->user_id();
    int64_t group_id = request->group_id();
    
    DBConn conn;
    
    // Check if member already
    std::string check = "SELECT id FROM im_group_member WHERE group_id=" + std::to_string(group_id) + 
                        " AND user_id=" + std::to_string(user_id);
    if (!mysql_query(conn.get(), check.c_str())) {
        MYSQL_RES* res = mysql_store_result(conn.get());
        if (res && mysql_fetch_row(res)) {
            mysql_free_result(res);
            reply->set_success(true); // Idempotent success
            return Status::OK;
        }
        if(res) mysql_free_result(res);
    }
    
    std::string sql = "INSERT INTO im_group_member (group_id, user_id, role) VALUES (" + 
                      std::to_string(group_id) + ", " + std::to_string(user_id) + ", 1)";
                      
    if (mysql_query(conn.get(), sql.c_str())) {
         reply->set_success(false);
         // if duplicate error (1062)，treat as success?
         if (mysql_errno(conn.get()) == 1062) {
             reply->set_success(true);
         }
    } else {
         reply->set_success(true);
         
         // Notify Group Members (System Message)
         tinyim::chat::SendMessageReq msg_req;
         msg_req.set_sender_id(user_id);
         msg_req.set_group_id(group_id);
         msg_req.set_type(tinyim::chat::SYSTEM); 
         msg_req.set_content("User " + std::to_string(user_id) + " joined the group");
         
         grpc::ClientContext ctx;
         tinyim::chat::SendMessageResp msg_resp;
         GetChatStub()->SendMessage(&ctx, msg_req, &msg_resp);
    }
    return Status::OK;
}

Status RelationServiceImpl::GetGroupList(ServerContext* context, const tinyim::relation::GetGroupListReq* request,
                                         tinyim::relation::GetGroupListResp* reply) {
    int64_t user_id = request->user_id();
    DBConn conn(DBConn::READ);
    
    std::string sql = "SELECT g.group_id, g.group_name, g.owner_id FROM im_group_member m "
                      "JOIN im_group g ON m.group_id = g.group_id "
                      "WHERE m.user_id = " + std::to_string(user_id);
                      
    if (mysql_query(conn.get(), sql.c_str())) return Status::OK;
    
    MYSQL_RES* res = mysql_store_result(conn.get());
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            auto* item = reply->add_groups();
            item->set_group_id(std::stoll(row[0]));
            item->set_group_name(row[1] ? row[1] : "");
            item->set_owner_id(std::stoll(row[2]));
        }
        mysql_free_result(res);
    }
    reply->set_success(true);
    return Status::OK;
}
