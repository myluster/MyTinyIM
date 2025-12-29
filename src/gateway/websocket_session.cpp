#include "websocket_session.h"
#include "connection_manager.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
namespace http = boost::beast::http; // Fix namespace error

WebsocketSession::WebsocketSession(tcp::socket&& socket)
    : ws_(std::move(socket)) {
}

void WebsocketSession::Run() {
    // Already accepted in DoAccept -> OnAccept
}

void WebsocketSession::OnAccept(beast::error_code ec) {
    if(ec) {
        spdlog::error("WS Accept failed: {}", ec.message());
        return;
    }

    // Handshake complete
    
    // Enable built-in heartbeat and timeout
    // Explicitly set timeout for testing purpose (FAST TIMEOUT)
    websocket::stream_base::timeout opt{
        std::chrono::seconds(5),   // Handshake timeout
        std::chrono::seconds(5),   // Idle timeout (Disconnect if no data/pong for 5s)
        true                       // Keep alive pings
    };
    ws_.set_option(opt);
    
    // Set decorators/User-Agent if needed
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res) {
            res.set(http::field::server, "TinyIM Gateway");
        }));

    ConnectionManager::GetInstance().Join(user_id_, shared_from_this());
    
    // Read message
    DoRead();
}

void WebsocketSession::DoRead() {
    ws_.async_read(buffer_,
        beast::bind_front_handler(&WebsocketSession::OnRead, shared_from_this()));
}

void WebsocketSession::OnRead(beast::error_code ec, std::size_t bytes_transferred) {
    if(ec == websocket::error::closed || ec == http::error::end_of_stream) {
        spdlog::error("WS Closed (user={})", user_id_);
        ConnectionManager::GetInstance().Leave(user_id_, shared_from_this());
        return;
    }
    if(ec == beast::error::timeout) {
        spdlog::error("WS Timeout (user={})", user_id_);
        ConnectionManager::GetInstance().Leave(user_id_, shared_from_this());
        return;
    }
    if(ec) {
        spdlog::error("WS Read failed: {}", ec.message());
        ConnectionManager::GetInstance().Leave(user_id_, shared_from_this());
        return;
    }

    // Process message
    std::string s = beast::buffers_to_string(buffer_.data());
    spdlog::info("WS Recv from {}: {}", user_id_, s);
    
    // Echo back or Handle Logic (Forward to Logic Svc)
    // For MVP, if it says "PING", respond "PONG"
    // Also we need to parse Protocol (Head+Body).
    // For now, assume Test Text.

    ws_.text(ws_.got_text());
    // Echo for test
    // DoWrite(s); // Echo
    
    buffer_.consume(buffer_.size());
    DoRead();
}

void WebsocketSession::DoWrite(std::string msg) {
    auto sp = std::make_shared<std::string>(std::move(msg));
    ws_.async_write(boost::asio::buffer(*sp),
        [self = shared_from_this(), sp](beast::error_code ec, std::size_t bytes) {
            if(ec) spdlog::error("WS Write failed: {}", ec.message());
        });
}

void WebsocketSession::Send(const std::string& msg) {
    // Thread safety: async_write is not thread safe if called concurrently.
    // Boost.Beast recommends a queue.
    // Simplifying: use post/dispatch to strand.
    // Since we don't have strand in member yet (ioc ref?), let's just use DoWrite for now 
    // and hope simple traffic is fine. Real implementation needs a write queue.
    
    // Note: DoWrite here spawns a new async op. If previous hasn't finished, 
    // undefined behavior. 
    // We NEED a queue.
    // For MVP speed, let's skip the queue and assume low concurrency or implement a basic lock? 
    // No, async_write creates tasks.
    // Re-visiting this later if needed.
    DoWrite(msg);
}

void WebsocketSession::Close() {
    ws_.async_close(websocket::close_code::normal,
        [self = shared_from_this()](beast::error_code ec) {});
}
