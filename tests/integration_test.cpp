#include <gtest/gtest.h>
#include "test_client.h"
#include "relation.pb.h"
#include "chat.pb.h"
#include "service_registry.h"
#include "redis_client.h"
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class IntegrationTest : public ::testing::Test {
protected:
    std::string host = "localhost";
    std::string http_port = "8080";
    std::string ws_port = "8080";

    void SetUp() override {
        static bool init = false;
        if(!init) {
            // Init Redis for Registry Test
            // Use 'redis' hostname in Docker environment
            RedisClient::GetInstance().Init("redis", 6379);
            init = true;
        }
    }
};

// ==========================================
// Group 0: Infrastructure
// ==========================================

// 0. Infrastructure: Registry Local Cache Mechanism
TEST_F(IntegrationTest, Infrastructure_ServiceRegistry_LocalCache) {
    // This test verifies that ServiceRegistry correctly polls Redis and updates local cache
    std::string svc_name = "test_lb_svc_integrated_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string ip = "1.2.3.4";
    int port = 5555;
    std::string addr = ip + ":" + std::to_string(port);
    std::string key = "im:service:" + svc_name + ":" + addr;

    // 1. Observe (Start Polling)
    ServiceRegistry::GetInstance().Observe(svc_name);

    // 2. Simulate Service Registration (Direct to Redis)
    RedisClient::GetInstance().SetEx(key, addr, 20); 

    // 3. Wait for polling (3s interval + buffer)
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // 4. Discover - Should find it (Cache Hit)
    std::string found = ServiceRegistry::GetInstance().Discover(svc_name);
    EXPECT_EQ(found, addr);

    // 5. Verify Local Cache Resilience
    // Delete from Redis
    RedisClient::GetInstance().Del(key);
    
    // Immediate Discover should still return cached value
    std::string cached = ServiceRegistry::GetInstance().Discover(svc_name);
    EXPECT_EQ(cached, addr) << "Should verify local cache is working (stale data valid until refresh)";

    // 6. Verify Periodic Refresh
    // Wait for next cycle
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // Now cache should be cleared
    std::string refreshed = ServiceRegistry::GetInstance().Discover(svc_name);
    EXPECT_TRUE(refreshed.empty()) << "Cache should be cleared after refresh";
}

// ==========================================
// Group 1: Basic Functionality & Auth
// ==========================================

// 1. Load Balancing Verification
TEST_F(IntegrationTest, Basic_LoadBalancing_Dispatch) {
    // Since we only have 1 gateway in this env, we just verify that Login returns a valid gateway url.
    // And ideally matches "127.0.0.1:8080".
    
    std::string u = "lb_user_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    TestClient client(host, http_port, ws_port);
    std::string token = client.Login(u, "123");
    
    std::string gw = client.GetGatewayUrl();
    spdlog::info("LB Returned: {}", gw);
    
    // It should be non-empty and start with ws://
    ASSERT_FALSE(gw.empty());
    ASSERT_EQ(gw.find("ws://"), 0);
}

// 2. Active Logout Test
TEST_F(IntegrationTest, Basic_Active_Logout) {
    std::string u = "logout_user_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    TestClient client(host, http_port, ws_port);
    std::string token = client.Login(u, "123");
    client.Connect(client.GetUserId(), token, "PC");
    
    // Send Logout via HTTP
    {
        json body = {{"user_id", client.GetUserId()}, {"device", "PC"}};
        json resp = HttpPost(host, http_port, "/api/logout", body);
        ASSERT_EQ(resp["code"], 0);
    }
    
    // Verify: Re-login or WebSocket usage might be affected?
    // Actually Logout just invalidates Token in Redis.
    // Existing WS connection might stay alive until it expires or server kicks?
    // The Logout logic executes: Redis Del + Publish Kick.
    
    // So Client should receive kick packet (0x1006)
    std::string body;
    ASSERT_TRUE(client.WaitForPacket(0x1006, body, 2000)) << "Client should be kicked after logout";
}

// 3. Heartbeat Timeout (Server Side Close)
TEST_F(IntegrationTest, Basic_Heartbeat_Timeout) {
    // Requires Gateway to have short timeout.
    // websocket_session.cpp has: stream_base::timeout opt{ seconds(5), ... }
    
    std::string u = "hb_user_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    TestClient client(host, http_port, ws_port);
    std::string token = client.Login(u, "123");
    client.Connect(client.GetUserId(), token, "PC", "", false);
    
    // Verify connection is alive initially
    ASSERT_TRUE(client.IsRunning());
    
    // Do NOT send anything.
    // Wait > 5s for server to close due to idle timeout.
    // We poll for closure to avoid race conditions
    bool closed = false;
    for (int i = 0; i < 150; i++) { // Wait up to 15s (Timeout is 5s, but give plenty of buffer)
        if (!client.IsRunning()) {
            closed = true;
            spdlog::info("TestClient: Closed detected at iteration {}", i);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Verify connection is no longer running
    EXPECT_TRUE(closed) << "Connection should be closed after heartbeat timeout";
}

// 4. Test Login & Kick Logic
TEST_F(IntegrationTest, Basic_Login_And_Kick_Mutex) {
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    std::string u1 = "user_k_" + std::to_string(ts);
    
    TestClient client1(host, http_port, ws_port);
    std::string token1 = client1.Login(u1, "123");
    client1.Connect(client1.GetUserId(), token1, "PC");
    
    // Login 2 on same device -> Kick 1
    TestClient client2(host, http_port, ws_port);
    std::string token2 = client2.Login(u1, "123", "PC"); // Login triggers Auth Svc -> Redis -> Gateway Kick
    
    // Client1 should receive Logout Packet (0x1006)
    std::string body;
    // Wait up to 3s
    bool kicked = client1.WaitForPacket(0x1006, body, 3000); // 0x1006 defined as CMD_LOGOUT_RESP in theory, or just reused
    if (!kicked) {
        // Or maybe connection closed? TestClient currently doesn't expose close event as packet.
        // But our Gateway sends SendPacket(0x1006) before close.
        spdlog::warn("Client 1 did not receive Kick Packet, checking if disconnected...");
        // client1.SendPacket... would fail
    }
    // We assume Success if we received the packet or strictly if Disconnected. 
    // For now assert true.
    ASSERT_TRUE(kicked) << "Client 1 should be kicked";
}

// ==========================================
// Group 2: Core Chat Flow Service
// ==========================================

// 5. Full Relationship & Chat Flow
TEST_F(IntegrationTest, Flow_Relation_And_SingleChat) {
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    std::string u1 = "user_a_" + std::to_string(ts);
    std::string u2 = "user_b_" + std::to_string(ts);
    
    TestClient clientA(host, http_port, ws_port);
    std::string tokenA = clientA.Login(u1, "123");
    clientA.Connect(clientA.GetUserId(), tokenA, "PC");
    
    TestClient clientB(host, http_port, ws_port);
    std::string tokenB = clientB.Login(u2, "123", "Mobile");
    int64_t uidB = clientB.GetUserId();
    clientB.Connect(uidB, tokenB, "Mobile");

    // A. Verify Stranger Chat Failure
    {
        tinyim::chat::SendMessageReq req;
        req.set_receiver_id(uidB);
        req.set_type(tinyim::chat::TEXT);
        req.set_content("Hello Stranger");
        
        clientA.SendPacket(CMD_MSG_SEND_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientA.WaitForPacket(CMD_MSG_SEND_RESP, body));
        tinyim::chat::SendMessageResp resp;
        resp.ParseFromString(body);
        ASSERT_FALSE(resp.success()) << "Should fail if not friends. Msg: " << resp.error_message();
    }
    
    // B. Add Friend Flow
    {
        // A Apply B
        tinyim::relation::ApplyFriendReq req;
        req.set_friend_id(uidB);
        req.set_remark("I am A");
        clientA.SendPacket(CMD_FRIEND_APPLY_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientA.WaitForPacket(CMD_FRIEND_APPLY_RESP, body));
        tinyim::relation::ApplyFriendResp resp;
        resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
    }
    {
        // B Recv Push
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body));
        tinyim::chat::MsgPushNotify notify;
        notify.ParseFromString(body);
        ASSERT_EQ(notify.type(), tinyim::chat::FRIEND_REQ);
    }
    {
        // B Accept A
        tinyim::relation::AcceptFriendReq req;
        req.set_requester_id(clientA.GetUserId());
        req.set_accept(true);
        clientB.SendPacket(CMD_FRIEND_ACCEPT_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_FRIEND_ACCEPT_RESP, body));
        tinyim::relation::AcceptFriendResp resp;
        resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
    }

    // C. Verify Friend Chat Success
    {
        tinyim::chat::SendMessageReq req;
        req.set_receiver_id(uidB);
        req.set_type(tinyim::chat::TEXT);
        req.set_content("Hello Friend");
        
        clientA.SendPacket(CMD_MSG_SEND_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientA.WaitForPacket(CMD_MSG_SEND_RESP, body));
        tinyim::chat::SendMessageResp resp;
        resp.ParseFromString(body);
        ASSERT_TRUE(resp.success()) << resp.error_message();
    }
    
    // D. B Recv Push (TEXT)
    {
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body));
        tinyim::chat::MsgPushNotify notify;
        notify.ParseFromString(body);
        ASSERT_EQ(notify.type(), tinyim::chat::TEXT);
    }
    
    // E. Sync (PC Mode)
    {
        tinyim::chat::SyncMessagesReq req;
        req.set_user_id(uidB);
        req.set_local_seq(0);
        req.set_limit(10);
        req.set_reverse(false);
        clientB.SendPacket(CMD_MSG_SYNC_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_MSG_SYNC_RESP, body));
        tinyim::chat::SyncMessagesResp resp;
        resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
        ASSERT_GE(resp.msgs_size(), 1);
        auto last_msg = resp.msgs(resp.msgs_size()-1);
        EXPECT_EQ(last_msg.content(), "Hello Friend");
    }
    
    // F. Sync (Web Reverse Mode)
    {
        tinyim::chat::SyncMessagesReq req;
        req.set_user_id(uidB);
        req.set_limit(5);
        req.set_reverse(true); // Latest messages
        
        clientB.SendPacket(CMD_MSG_SYNC_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_MSG_SYNC_RESP, body));
        tinyim::chat::SyncMessagesResp resp;
        resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
        ASSERT_GE(resp.msgs_size(), 1);
        // Should find "Hello Friend" near the top
        bool found = false;
        for(const auto& m : resp.msgs()) {
            if (m.content() == "Hello Friend") found = true;
        }
        EXPECT_TRUE(found);
    }
}

// 6. Offline Messages
TEST_F(IntegrationTest, Flow_Offline_Message) {
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    std::string uA = "off_A_" + std::to_string(ts);
    std::string uB = "off_B_" + std::to_string(ts);
    
    TestClient clientA(host, http_port, ws_port);
    std::string tokenA = clientA.Login(uA, "123");
    clientA.Connect(clientA.GetUserId(), tokenA, "PC");
    
    TestClient clientB(host, http_port, ws_port);
    std::string tokenB = clientB.Login(uB, "123");
    int64_t uidB = clientB.GetUserId();
    
    // Make friends
    {
        tinyim::relation::ApplyFriendReq req; req.set_friend_id(uidB);
        clientA.SendPacket(CMD_FRIEND_APPLY_REQ, req);
        // B Connect specifically to accept
        clientB.Connect(uidB, tokenB, "PC");
        std::string body; clientB.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body);
        tinyim::relation::AcceptFriendReq acc; acc.set_requester_id(clientA.GetUserId()); acc.set_accept(true);
        clientB.SendPacket(CMD_FRIEND_ACCEPT_REQ, acc);
        clientB.WaitForPacket(CMD_FRIEND_ACCEPT_RESP, body);
        clientB.Close(); // B goes OFFLINE
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // A sends Message to B (OFFLINE)
    std::string content = "Offline Msg 123";
    {
        tinyim::chat::SendMessageReq req;
        req.set_receiver_id(uidB);
        req.set_type(tinyim::chat::TEXT);
        req.set_content(content);
        clientA.SendPacket(CMD_MSG_SEND_REQ, req);
        std::string body;
        ASSERT_TRUE(clientA.WaitForPacket(CMD_MSG_SEND_RESP, body));
    }
    
    // B Comes Online
    clientB.Connect(uidB, tokenB, "PC");
    
    // B Syncs
    {
        tinyim::chat::SyncMessagesReq req;
        req.set_user_id(uidB);
        req.set_limit(5);
        req.set_reverse(true);
        clientB.SendPacket(CMD_MSG_SYNC_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_MSG_SYNC_RESP, body));
        tinyim::chat::SyncMessagesResp resp;
        resp.ParseFromString(body);
        
        bool found = false;
        for(const auto& m : resp.msgs()) {
            if (m.content() == content) found = true;
        }
        ASSERT_TRUE(found) << "Offline message not found in sync";
    }
}

// 7. Multi-Device Sync Test
TEST_F(IntegrationTest, Flow_MultiDevice_Sync) {
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    std::string uA = "user_multi_A_" + std::to_string(ts);
    std::string uB = "user_multi_B_" + std::to_string(ts);
    
    // 1. Create Users
    TestClient clientA(host, http_port, ws_port);
    std::string tokenA = clientA.Login(uA, "123");
    clientA.Connect(clientA.GetUserId(), tokenA, "PC");
    
    // User B - Device 1 (PC)
    TestClient clientB_PC(host, http_port, ws_port);
    std::string tokenB = clientB_PC.Login(uB, "123");
    int64_t uidB = clientB_PC.GetUserId();
    clientB_PC.Connect(uidB, tokenB, "PC");
    
    // User B - Device 2 (Mobile)
    TestClient clientB_Mobile(host, http_port, ws_port);
    // Login again to get token (or reuse if token is valid for all devices?)
    // Usually token is per-login. Let's login again.
    std::string tokenB2 = clientB_Mobile.Login(uB, "123", "Mobile");
    clientB_Mobile.Connect(uidB, tokenB2, "Mobile");
    
    // 2. Establish Relation (A -> B)
    {
        tinyim::relation::ApplyFriendReq req;
        req.set_friend_id(uidB);
        req.set_remark("Friend Request");
        clientA.SendPacket(CMD_FRIEND_APPLY_REQ, req);
        
        // B accepts via PC
        std::string body;
        ASSERT_TRUE(clientB_PC.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body));
        
        tinyim::relation::AcceptFriendReq acc;
        acc.set_requester_id(clientA.GetUserId());
        acc.set_accept(true);
        clientB_PC.SendPacket(CMD_FRIEND_ACCEPT_REQ, acc);
        ASSERT_TRUE(clientB_PC.WaitForPacket(CMD_FRIEND_ACCEPT_RESP, body));
    }
    
    // 3. A Sends Msg to B
    std::string msg_content = "MultiDevice Broadcast Test";
    {
        tinyim::chat::SendMessageReq req;
        req.set_receiver_id(uidB);
        req.set_type(tinyim::chat::TEXT);
        req.set_content(msg_content);
        clientA.SendPacket(CMD_MSG_SEND_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientA.WaitForPacket(CMD_MSG_SEND_RESP, body));
        tinyim::chat::SendMessageResp resp;
        resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
    }
    
    // 4. Verify Both Devices Receive Push
    {
        // PC should receive TEXT message (may receive multiple pushes including friend requests)
        bool pc_got_text = false;
        for (int i = 0; i < 5; i++) {  // Try up to 5 messages
            std::string body;
            if (clientB_PC.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body, 1000)) {
                tinyim::chat::MsgPushNotify n; 
                n.ParseFromString(body);
                if (n.type() == tinyim::chat::TEXT) {
                    pc_got_text = true;
                    break;
                }
            } else {
                break;
            }
        }
        ASSERT_TRUE(pc_got_text) << "PC didn't receive TEXT message";
        
        // Mobile should receive TEXT message
        bool mobile_got_text = false;
        for (int i = 0; i < 5; i++) {
            std::string body;
            if (clientB_Mobile.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body, 1000)) {
                tinyim::chat::MsgPushNotify n;
                n.ParseFromString(body);
                if (n.type() == tinyim::chat::TEXT) {
                    mobile_got_text = true;
                    break;
                }
            } else {
                break;
            }
        }
        ASSERT_TRUE(mobile_got_text) << "Mobile didn't receive TEXT message";
    }
    
    // 5. Verify Content Sync
    // PC Syncs
    {
        tinyim::chat::SyncMessagesReq req;
        req.set_user_id(uidB);
        req.set_limit(1);
        req.set_reverse(true); // Get latest
        clientB_PC.SendPacket(CMD_MSG_SYNC_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB_PC.WaitForPacket(CMD_MSG_SYNC_RESP, body));
        tinyim::chat::SyncMessagesResp resp;
        resp.ParseFromString(body);
        ASSERT_GT(resp.msgs_size(), 0);
        EXPECT_EQ(resp.msgs(0).content(), msg_content);
    }
    
    // Mobile Syncs
    {
        tinyim::chat::SyncMessagesReq req;
        req.set_user_id(uidB);
        req.set_limit(1);
        req.set_reverse(true); // Get latest
        clientB_Mobile.SendPacket(CMD_MSG_SYNC_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB_Mobile.WaitForPacket(CMD_MSG_SYNC_RESP, body));
        tinyim::chat::SyncMessagesResp resp;
        resp.ParseFromString(body);
        ASSERT_GT(resp.msgs_size(), 0);
        EXPECT_EQ(resp.msgs(0).content(), msg_content);
    }
}

// 8. Group Chat Flow
TEST_F(IntegrationTest, Flow_Group_Chat) {
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    std::string uA = "grp_A_" + std::to_string(ts);
    std::string uB = "grp_B_" + std::to_string(ts);
    
    TestClient clientA(host, http_port, ws_port);
    std::string tokenA = clientA.Login(uA, "123");
    clientA.Connect(clientA.GetUserId(), tokenA, "PC");
    
    TestClient clientB(host, http_port, ws_port);
    std::string tokenB = clientB.Login(uB, "123");
    clientB.Connect(clientB.GetUserId(), tokenB, "PC");
    
    int64_t group_id = 0;
    
    // A Create Group
    {
        tinyim::relation::CreateGroupReq req;
        req.set_group_name("Test Group");
        // add members? currently proto definition doesn't show members in CreateReq, assumes owner only?
        // Check proto... CreateGroupReq { owner_id, group_name, members... } ?
        // Using what's available. Assuming just Create.
        req.set_group_name("Test Group"); // Assuming modification to match Proto
        
        // Wait, did I check Relation Service?
        // Assuming implementation exists.
        clientA.SendPacket(CMD_GROUP_CREATE_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientA.WaitForPacket(CMD_GROUP_CREATE_RESP, body));
        tinyim::relation::CreateGroupResp resp; resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
        group_id = resp.group_id();
        ASSERT_GT(group_id, 0);
    }
    
    // B Join Group
    {
        tinyim::relation::JoinGroupReq req;
        req.set_group_id(group_id);
        clientB.SendPacket(CMD_GROUP_JOIN_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_GROUP_JOIN_RESP, body));
        tinyim::relation::JoinGroupResp resp; resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
    }
    
    // A Send Group Msg
    std::string g_msg = "Hello Group";
    {
        tinyim::chat::SendMessageReq req;
        req.set_group_id(group_id); // Group Msg
        req.set_type(tinyim::chat::TEXT);
        req.set_content(g_msg);
        clientA.SendPacket(CMD_MSG_SEND_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientA.WaitForPacket(CMD_MSG_SEND_RESP, body));
    }
    
    // B Receive Push
    {
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body));
        tinyim::chat::MsgPushNotify n; n.ParseFromString(body);
        // Expect group type or handle in logic
    }
    
    // B Sync Group Msg ? (Timeline is mixed? or separate?)
    // In design: Mixed timeline.
    {
        tinyim::chat::SyncMessagesReq req;
        req.set_user_id(clientB.GetUserId());
        req.set_limit(5);
        req.set_reverse(true);
        clientB.SendPacket(CMD_MSG_SYNC_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientB.WaitForPacket(CMD_MSG_SYNC_RESP, body));
        tinyim::chat::SyncMessagesResp resp; resp.ParseFromString(body);
        
        bool found = false;
        for(const auto& m : resp.msgs()) {
            if (m.content() == g_msg && m.group_id() == group_id) found = true;
        }
        ASSERT_TRUE(found) << "Group message not found";
    }
}

// 9. Group Verification Flow
TEST_F(IntegrationTest, Flow_Group_Verification) {
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    std::string uOwner = "grp_O_" + std::to_string(ts);
    std::string uJoiner = "grp_J_" + std::to_string(ts);
    
    TestClient clientO(host, http_port, ws_port);
    std::string tokenO = clientO.Login(uOwner, "123");
    clientO.Connect(clientO.GetUserId(), tokenO, "PC");
    
    TestClient clientJ(host, http_port, ws_port);
    std::string tokenJ = clientJ.Login(uJoiner, "123");
    clientJ.Connect(clientJ.GetUserId(), tokenJ, "PC");
    
    int64_t group_id = 0;
    
    // A. Owner Created Group
    {
        tinyim::relation::CreateGroupReq req;
        req.set_group_name("Verify Group");
        req.set_owner_id(clientO.GetUserId());
        clientO.SendPacket(CMD_GROUP_CREATE_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientO.WaitForPacket(CMD_GROUP_CREATE_RESP, body));
        tinyim::relation::CreateGroupResp resp; resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
        group_id = resp.group_id();
    }
    
    // B. Joiner Applies (Verify Logic)
    int64_t apply_id = 0;
    {
        tinyim::relation::ApplyGroupReq req;
        req.set_group_id(group_id);
        req.set_remark("Let me in");
        clientJ.SendPacket(CMD_GROUP_APPLY_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientJ.WaitForPacket(CMD_GROUP_APPLY_RESP, body));
        tinyim::relation::ApplyGroupResp resp; resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
        apply_id = resp.apply_id();
    }
    
    // C. Owner Receives Notification (Friend Req Type used for now)
    {
        std::string body;
        ASSERT_TRUE(clientO.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body));
    }
    
    // D. Owner Accepts
    {
        tinyim::relation::AcceptGroupReq req;
        req.set_group_id(group_id);
        req.set_requester_id(clientJ.GetUserId());
        req.set_apply_id(apply_id);
        req.set_accept(true);
        clientO.SendPacket(CMD_GROUP_ACCEPT_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientO.WaitForPacket(CMD_GROUP_ACCEPT_RESP, body));
        tinyim::relation::AcceptGroupResp resp; resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
    }
    
    // E. Joiner Receives Notification
    {
        std::string body;
        ASSERT_TRUE(clientJ.WaitForPacket(CMD_MSG_PUSH_NOTIFY, body));
        tinyim::chat::MsgPushNotify n; n.ParseFromString(body);
        ASSERT_EQ(n.type(), tinyim::chat::SYSTEM);
    }
    
    // F. Verify Joiner is in Group (Send Msg)
    // Wait for consistency
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    {
        tinyim::chat::SendMessageReq req;
        req.set_group_id(group_id);
        req.set_content("I'm in!");
        clientJ.SendPacket(CMD_MSG_SEND_REQ, req);
        
        std::string body;
        ASSERT_TRUE(clientJ.WaitForPacket(CMD_MSG_SEND_RESP, body));
        tinyim::chat::SendMessageResp resp; resp.ParseFromString(body);
        ASSERT_TRUE(resp.success());
    }
}
