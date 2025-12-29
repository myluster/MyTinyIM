#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WebsocketSession : public std::enable_shared_from_this<WebsocketSession> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::string device_; // Device info from Token/Param
    int64_t user_id_;

public:
    explicit WebsocketSession(tcp::socket&& socket);
    
    // Accept upgrade from HTTP
    // We take ownership of the upgraded stream presumably, or the request
    template<class Body, class Allocator>
    void DoAccept(beast::http::request<Body, beast::http::basic_fields<Allocator>> req) {
        ws_.async_accept(req, beast::bind_front_handler(&WebsocketSession::OnAccept, shared_from_this()));
    }

    void Run();
    void Send(const std::string& msg);
    void Close();
    void SetUserInfo(int64_t uid, std::string dev) { user_id_ = uid; device_ = dev; }

    int64_t GetUserId() const { return user_id_; }
    std::string GetDevice() const { return device_; }

private:
    void OnAccept(beast::error_code ec);
    void DoRead();
    void OnRead(beast::error_code ec, std::size_t bytes_transferred);
    void DoWrite(std::string msg);
};
