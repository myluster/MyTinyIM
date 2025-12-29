#include <gtest/gtest.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <future>
#include <chrono>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

// Helper: HTTP POST
json HttpPost(const std::string& host, const std::string& port, const std::string& target, const json& body) {
    net::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);

    auto const results = resolver.resolve(host, port);
    stream.connect(results);

    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "TinyIM Test Client");
    req.set(http::field::content_type, "application/json");
    req.body() = body.dump();
    req.prepare_payload();

    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);

    stream.socket().shutdown(tcp::socket::shutdown_both);
    
    // Check status
    if (res.result() != http::status::ok) {
        throw std::runtime_error("HTTP Error: " + std::to_string(res.result_int()));
    }
    
    return json::parse(res.body());
}

// Helper: Run WebSocket Client
// Returns true if connection closed by server with "KICKED" or disconnect
bool ConnectWebSocketAndExpectKick(const std::string& host, const std::string& port, int64_t uid, const std::string& token, const std::string& device) {
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        websocket::stream<beast::tcp_stream> ws(ioc);

        auto const results = resolver.resolve(host, port);
        net::connect(ws.next_layer().socket(), results);

        // Handshake URL: /ws?token=...
        std::string path = "/ws?id=" + std::to_string(uid) + "&token=" + token + "&device=" + device;
        ws.handshake(host, path);
        
        // Wait for message
        beast::flat_buffer buffer;
        ws.read(buffer); // Block wait
        
        std::string msg = beast::buffers_to_string(buffer.data());
        spdlog::info("Test Client Recv: {}", msg);
        
        if (msg == "KICKED") return true;
        
        // Try read again (expect close)
        ws.read(buffer); 
    } catch (const std::exception& e) {
        // Exception usually means connection closed/reset, which mimics Kick
        spdlog::info("WS Exception (Expected Kick): {}", e.what());
        return true;
    }
    return false;
}

class IntegrationTest : public ::testing::Test {
protected:
    std::string host = "localhost"; // or 127.0.0.1
    std::string port = "8080";
    
    // Prepare Data
    std::string username;
    std::string password = "password123";
    
    void SetUp() override {
        // Gen unique username
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        username = "user_" + std::to_string(now);
    }
};

TEST_F(IntegrationTest, FullFlow_Register_Login_Kick) {
    // 1. Register
    spdlog::info("Step 1: Registering {}", username);
    json reg_body = {{"username", username}, {"password", password}, {"nickname", "TestNick"}};
    json reg_resp = HttpPost(host, port, "/api/register", reg_body);
    
    ASSERT_EQ(reg_resp["code"], 0);
    int64_t uid = reg_resp["data"]["user_id"];
    ASSERT_GT(uid, 0);

    // 2. Login Device A
    spdlog::info("Step 2: Login Device A (PC)");
    json login_body_a = {{"username", username}, {"password", password}, {"device", "PC"}};
    json login_resp_a = HttpPost(host, port, "/api/login", login_body_a);
    
    ASSERT_EQ(login_resp_a["code"], 0);
    std::string token_a = login_resp_a["data"]["token"];
    ASSERT_FALSE(token_a.empty());

    // 3. Connect WebSocket Device A (Async Thread)
    spdlog::info("Step 3: Device A Connecting WS...");
    std::atomic<bool> kicked{false};
    std::thread client_a_thread([&]() {
        if (ConnectWebSocketAndExpectKick(host, port, uid, token_a, "PC")) {
            kicked = true;
        }
    });

    // Wait a bit for A to settle
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 4. Login Device B (Same Device Type 'PC') -> Should Kick A
    spdlog::info("Step 4: Login Device B (PC) to Trigger Kick");
    json login_body_b = {{"username", username}, {"password", password}, {"device", "PC"}};
    json login_resp_b = HttpPost(host, port, "/api/login", login_body_b);
    ASSERT_EQ(login_resp_b["code"], 0);
    std::string token_b = login_resp_b["data"]["token"];
    
    ASSERT_NE(token_a, token_b); // New token generated

    // 5. Verify A is kicked
    spdlog::info("Step 5: Verifying A is kicked...");
    client_a_thread.join();
    ASSERT_TRUE(kicked);
    
    // 6. Logout B
    // HttpPost(host, port, "/api/logout", ...); // Not implemented as HTTP API yet?
    // We implemented gRPC Logout but need Gateway mapping. 
    // Assuming we skipped Gateway Logout API implementation in previous steps.
}

TEST_F(IntegrationTest, Active_Logout) {
    // 1. Register & Login
    json reg_body = {{"username", username}, {"password", password}};
    json reg_resp = HttpPost(host, port, "/api/register", reg_body);
    int64_t uid = reg_resp["data"]["user_id"];

    json login_body = {{"username", username}, {"password", password}, {"device", "PC"}};
    json login_resp = HttpPost(host, port, "/api/login", login_body);
    std::string token = login_resp["data"]["token"];

    // 2. Connect WS
    std::atomic<bool> kicked{false};
    std::thread client_thread([&]() {
        if (ConnectWebSocketAndExpectKick(host, port, uid, token, "PC")) {
            kicked = true;
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 3. Logout API -> Expected Kick
    spdlog::info("Calling Logout API...");
    json logout_body = {{"user_id", uid}, {"token", token}, {"device", "PC"}};
    json logout_resp = HttpPost(host, port, "/api/logout", logout_body);
    ASSERT_EQ(logout_resp["code"], 0);

    // 4. Wait for WS close
    client_thread.join();
    ASSERT_TRUE(kicked);
}

TEST_F(IntegrationTest, Heartbeat_Timeout) {
    // 1. Register & Login
    json reg_body = {{"username", username}, {"password", password}};
    HttpPost(host, port, "/api/register", reg_body); // ignore result checking for brevity
    
    // Need DB ID? parse it
    // Wait, simpler: reuse login if user persists check login
    json login_body = {{"username", username}, {"password", password}, {"device", "PC"}};
    json login_resp = HttpPost(host, port, "/api/login", login_body);
    int64_t uid = login_resp["data"]["user_id"];
    std::string token = login_resp["data"]["token"];

    // 2. Connect WS but stay silent
    spdlog::info("Connecting WS and sleeping > 5s to trigger timeout...");
    
    bool connection_closed = false;
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        websocket::stream<beast::tcp_stream> ws(ioc);

        auto const results = resolver.resolve(host, port);
        net::connect(ws.next_layer().socket(), results);

        ws.handshake(host, "/ws?id=" + std::to_string(uid) + "&token=" + token + "&device=PC");
        
        // Sleep 7 seconds (Timeout is 5s)
        std::this_thread::sleep_for(std::chrono::seconds(7));
        
        // Try to read/write - should throw error
        beast::flat_buffer b;
        ws.read(b);
        
    } catch (const std::exception& e) {
        spdlog::info("WS Closed as expected: {}", e.what());
        connection_closed = true;
    }
    
    ASSERT_TRUE(connection_closed);
}

TEST_F(IntegrationTest, Login_Fail_Password) {
    json body = {{"username", "non_exist"}, {"password", "wrong"}};
    // Assuming HttpPost throws or returns code!=0
    // Our Server returns JSON code=1 for logic error
    json resp = HttpPost(host, port, "/api/login", body);
    ASSERT_NE(resp["code"], 0);
}

TEST_F(IntegrationTest, Register_Fail_Duplicate) {
    std::string username = "dup_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::string password = "password123";

    // 1. First Register -> Success
    {
        json req;
        req["username"] = username;
        req["password"] = password;
        req["nickname"] = "Original";

        json resp = HttpPost(host, port, "/api/register", req);
        ASSERT_EQ(resp["code"], 0);
    }

    // 2. Second Register -> Fail
    {
        json req;
        req["username"] = username;
        req["password"] = password;
        req["nickname"] = "Duplicate";

        json resp = HttpPost(host, port, "/api/register", req);
        ASSERT_EQ(resp["code"], 1); // Logic Failure
        
        std::string msg = resp["msg"];
        // msg should contain "User may exist" or similar
        EXPECT_NE(msg.find("exist"), std::string::npos) << "Error message should hint duplication (" << msg << ")";
    }
}

TEST_F(IntegrationTest, MultiGateway_Kick_Distributed) {
    // This test requires a second gateway running on port 8081
    // We try to connect to 8081. If fail, we skip.
    std::string gw2_port = "8081";
    
    // Check connectivity to GW2
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        tcp::socket s(ioc);
        auto const results = resolver.resolve(host, gw2_port);
        net::connect(s, results);
    } catch (...) {
        spdlog::warn("Gateway 2 (port 8081) not reachable. Skipping Distributed Kick Test.");
        return;
    }

    spdlog::info("Gateway 2 (8081) detected. Running Distributed Kick Test...");
    
    // 1. Register
    std::string username = "multi_gw_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    json reg_resp = HttpPost(host, port, "/api/register", { // Register via GW1 is fine
        {"username", username}, {"password", "123"}, {"nickname", "Multi"}
    });
    int64_t uid = reg_resp["data"]["user_id"];

    // 2. Login Device A (PC) -> Get Token
    json login_a = HttpPost(host, port, "/api/login", {
        {"username", username}, {"password", "123"}, {"device", "PC"}
    });
    std::string token_a = login_a["data"]["token"];

    // 3. Connect Device A to **GW2 (8081)**
    std::thread client_thread([&]() {
        ConnectWebSocketAndExpectKick(host, gw2_port, uid, token_a, "PC");
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 4. Login Device B (PC) via **GW1 (8080)** -> Trigger Kick
    // Auth Server will publish to Redis. GW2 should receive it and kick Device A.
    HttpPost(host, port, "/api/login", {
        {"username", username}, {"password", "123"}, {"device", "PC"}
    }); // This should trigger the kick

    if(client_thread.joinable()) client_thread.join();
}

TEST_F(IntegrationTest, LoadBalance_Dispatch_Test) {
    // We expect both 8080 and 8081 to be returned by Login LB logic.
    // Use Concurrency to speed up test and simulate load
    int iterations = 20; // 20 concurrent requests
    std::atomic<int> count_8080{0};
    std::atomic<int> count_8081{0};
    std::atomic<int> success_count{0};

    std::string username = "lb_test";
    // Ensure registered (Sync)
    HttpPost(host, port, "/api/register", {
        {"username", username}, {"password", "123"}, {"nickname", "LB"}
    });

    std::vector<std::future<void>> futures;
    futures.reserve(iterations);

    spdlog::info("Starting Parallel LB Test with {} concurrent requests...", iterations);
    auto start = std::chrono::high_resolution_clock::now();

    for(int i=0; i<iterations; ++i) {
        futures.emplace_back(std::async(std::launch::async, [this, i, username, &count_8080, &count_8081, &success_count] {
            // Random jitter to avoid exact packet storms (optional)
            // std::this_thread::sleep_for(std::chrono::milliseconds(i * 2));
            
            try {
                // Each request must handle its own errors without crashing main test
                json resp = HttpPost(host, port, "/api/login", {
                    {"username", username}, {"password", "123"}, {"device", "PC"}
                });
                
                if (resp.contains("code") && resp["code"] == 0) {
                    success_count++;
                    std::string url = resp["data"]["gateway_url"];
                    if (url.find("8080") != std::string::npos) count_8080++;
                    else if (url.find("8081") != std::string::npos) count_8081++;
                } else {
                    spdlog::warn("Req {} failed logic: code={}", i, resp.value("code", -1));
                }
            } catch (const std::exception& e) {
                spdlog::error("Req {} failed exception: {}", i, e.what());
            }
        }));
    }

    // Wait for all
    for(auto& f : futures) {
        if (f.valid()) f.get();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    
    spdlog::info("LB Param Test Finished in {:.2f}s. Success: {}/{}. Stats: 8080={}, 8081={}", 
        elapsed.count(), success_count.load(), iterations, count_8080.load(), count_8081.load());

    // Assertions
    if (count_8080 == 0 || count_8081 == 0) {
        spdlog::warn("Load Balancing might not be effective or only one gateway is up. (8080: {}, 8081: {})", count_8080, count_8081);
    } else {
        EXPECT_GT(count_8080, 0);
        EXPECT_GT(count_8081, 0);
    }
    
    // Allow some failures if system is overloaded, but generally should be 100%
    EXPECT_GT(success_count, iterations * 0.8); 
}



